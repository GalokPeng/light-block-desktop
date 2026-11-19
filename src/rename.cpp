#include "rename.hpp"
#include "network.h"
#include <set>
namespace lb::files {
namespace {
std::wstring key(std::filesystem::path const& path) { auto value=std::filesystem::absolute(path).lexically_normal().wstring();if(!value.empty()) CharLowerBuffW(value.data(),DWORD(value.size()));return value; }
unique_handle open_item(std::filesystem::path const& path,DWORD access) {
    return unique_handle(CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
}
std::wstring identity(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Cannot identify file or unsupported reparse point");
    return std::to_wstring(info.dwVolumeSerialNumber)+L":"+std::to_wstring(info.nFileIndexHigh)+L":"+std::to_wstring(info.nFileIndexLow);
}
std::wstring identity(std::filesystem::path const& path) {return identity(open_item(path,FILE_READ_ATTRIBUTES).get());}
bool present(std::filesystem::path const& path) { return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES; }
std::filesystem::path temporary(std::filesystem::path const& source) {
    GUID guid{};if(FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot allocate rename identity");wchar_t text[40];StringFromGUID2(guid,text,40);return source.parent_path()/(L".LightBlockNative-rename-"+std::wstring(text));
}
void move(std::filesystem::path const& from,std::filesystem::path const& to,std::wstring const& expected) {
    // Use the same handle for validation and rename. The path can be replaced
    // between two system calls, especially on a redirected desktop at sign-in.
    auto source=open_item(from,DELETE|FILE_READ_ATTRIBUTES);
    if(!source||identity(source.get())!=expected) throw std::runtime_error("Rename source changed");
    auto destination=std::filesystem::absolute(to).wstring();
    auto name_bytes=destination.size()*sizeof(wchar_t);
    std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO)+name_bytes);
    auto info=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=DWORD(name_bytes);
    memcpy(info->FileName,destination.c_str(),name_bytes+sizeof(wchar_t));
    if(!SetFileInformationByHandle(source.get(),FileRenameInfo,info,DWORD(buffer.size()))) throw std::runtime_error("Rename failed; recovery record preserved");
}
std::wstring format(std::filesystem::path const& source,std::wstring const& pattern,size_t index) {
    std::wstring value;bool directory=(GetFileAttributesW(source.c_str())&FILE_ATTRIBUTE_DIRECTORY)!=0;
    for(size_t n=0;n<pattern.size();) {
        if(pattern[n]==L'{'&&n+1<pattern.size()&&pattern[n+1]==L'{') { value+=L'{';n+=2;continue; }
        if(pattern[n]==L'}'&&n+1<pattern.size()&&pattern[n+1]==L'}') { value+=L'}';n+=2;continue; }
        if(pattern[n]!=L'{') { if(pattern[n]==L'}') throw std::runtime_error("Invalid rename pattern");value+=pattern[n++];continue; }
        size_t end=pattern.find(L'}',n+1);if(end==std::wstring::npos) throw std::runtime_error("Invalid rename pattern");auto field=pattern.substr(n+1,end-n-1);n=end+1;
        if(field==L"name") value+=directory?source.filename().wstring():source.stem().wstring();
        else if(field==L"ext") { if(!directory) value+=source.extension().wstring(); }
        else if(field==L"n") value+=std::to_wstring(index);
        else if(field.starts_with(L"n:")) {
            auto spec=field.substr(2);if(spec.ends_with(L"d")) spec.pop_back();if(spec.empty()||spec.size()>2||!std::all_of(spec.begin(),spec.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})) throw std::runtime_error("Only name, ext and n:03 are supported");
            int width=_wtoi(spec.c_str());if(width>32) throw std::runtime_error("Number width too large");auto number=std::to_wstring(index);if(number.size()<size_t(width)) value.append(size_t(width)-number.size(),spec[0]==L'0'?L'0':L' ');value+=number;
        }else throw std::runtime_error("Only name, ext and n:03 are supported");
        if(value.size()>255) throw std::runtime_error("Filename too long");
    }
    if(!safe_filename(value.c_str())) throw std::runtime_error("Invalid or reserved filename");return value;
}
struct Record { std::filesystem::path source,target,temp,rescue;std::wstring id; };
}
std::vector<Pair> preview(std::vector<std::filesystem::path> const& paths,std::wstring const& pattern) {
    if(paths.empty()||paths.size()>1000||pattern.size()>256) throw std::runtime_error("Select between 1 and 1000 files");std::set<std::wstring> sources,targets,ids;std::vector<Pair> pairs;
    for(auto const& path:paths) if(!sources.insert(key(path)).second) throw std::runtime_error("Duplicate source");
    for(size_t n=0;n<paths.size();++n) {
        auto source=std::filesystem::absolute(paths[n]).lexically_normal();auto id=identity(source);auto target=source.parent_path()/format(source,pattern,n+1);
        if(!ids.insert(id).second||!targets.insert(key(target)).second||(present(target)&&!sources.contains(key(target)))) throw std::runtime_error("Duplicate, hard-linked or occupied target");pairs.push_back({source,target,std::move(id)});
    }return pairs;
}
Journal::Journal(std::filesystem::path const& database):db_(database) {
    db_.exec("CREATE TABLE IF NOT EXISTS rename_jobs(id INTEGER PRIMARY KEY,state TEXT NOT NULL,undo_of INTEGER NOT NULL DEFAULT 0);CREATE TABLE IF NOT EXISTS rename_items(job INTEGER NOT NULL,n INTEGER NOT NULL,source TEXT NOT NULL,target TEXT NOT NULL,temp TEXT NOT NULL,rescue TEXT NOT NULL DEFAULT '',identity TEXT NOT NULL,PRIMARY KEY(job,n));CREATE TABLE IF NOT EXISTS rename_locations(job INTEGER NOT NULL,n INTEGER NOT NULL,path TEXT NOT NULL,PRIMARY KEY(job,n,path));");
}
int64_t Journal::apply(std::vector<Pair> const& pairs,int64_t undo_of,int interrupt_after) {
    if(pairs.empty()||pairs.size()>1000) throw std::runtime_error("Invalid rename batch");if(!pending().empty()) throw std::runtime_error("Recover interrupted jobs first");
    std::set<std::wstring> sources,targets,ids;std::vector<Record> records;
    for(auto const& p:pairs) {
        if(!sources.insert(key(p.source)).second||!targets.insert(key(p.target)).second||!ids.insert(p.identity).second||identity(p.source)!=p.identity||key(p.source.parent_path())!=key(p.target.parent_path())||!safe_filename(p.target.filename().c_str())) throw std::runtime_error("Rename preview is stale or invalid");
        if(p.source.wstring()!=p.target.wstring()) records.push_back({p.source,p.target,temporary(p.source),{},p.identity});
    }
    for(auto const& p:pairs) if(present(p.target)&&!sources.contains(key(p.target))) throw std::runtime_error("Target is occupied");
    for(auto const& a:pairs) for(auto const& b:pairs) if(&a!=&b&&key(b.source).starts_with(key(a.source)+L"\\")) throw std::runtime_error("Do not rename parent and descendant together");
    if(records.empty()) return 0;int64_t job{};
    { sql::Transaction transaction(db_);sql::Statement add(db_,"INSERT INTO rename_jobs(state,undo_of) VALUES('pending',?)");add.bind(1,undo_of);add.next();sql::Statement query(db_,"SELECT last_insert_rowid()");query.next();job=query.integer(0);
      for(size_t n=0;n<records.size();++n) { auto const& r=records[n];sql::Statement item(db_,"INSERT INTO rename_items(job,n,source,target,temp,identity) VALUES(?,?,?,?,?,?)");item.bind(1,job);item.bind(2,int64_t(n));item.bind(3,r.source.wstring());item.bind(4,r.target.wstring());item.bind(5,r.temp.wstring());item.bind(6,r.id);item.next(); }transaction.commit(); }
    int moves=0;
    try {
        if(records.size()==1 && key(records.front().source)!=key(records.front().target)) {
            auto const& r=records.front();move(r.source,r.target,r.id);
            if(interrupt_after==1) return job;
        }else {
            for(auto const& r:records) { move(r.source,r.temp,r.id);if(interrupt_after>=0&&++moves==interrupt_after) return job; }
            for(auto const& r:records) { move(r.temp,r.target,r.id);if(interrupt_after>=0&&++moves==interrupt_after) return job; }
        }
        sql::Statement committed(db_,"UPDATE rename_jobs SET state='committed' WHERE id=?");committed.bind(1,job);committed.next();return job;
    }catch(...) { try { restore(job); }catch(...) {} throw; }
}
std::vector<int64_t> Journal::pending() { std::vector<int64_t> result;sql::Statement rows(db_,"SELECT id FROM rename_jobs WHERE state IN ('pending','recovering') ORDER BY id DESC LIMIT 1000");while(rows.next()) result.push_back(rows.integer(0));return result; }
int64_t Journal::latest() { sql::Statement row(db_,"SELECT id FROM rename_jobs j WHERE state='committed' AND undo_of=0 AND NOT EXISTS(SELECT 1 FROM rename_jobs u WHERE u.undo_of=j.id AND u.state='committed') ORDER BY id DESC LIMIT 1");return row.next()?row.integer(0):0; }
void Journal::restore(int64_t job) {
    std::vector<Record> records;std::vector<std::filesystem::path> located;std::set<std::wstring> moving;{
        sql::Statement rows(db_,"SELECT source,target,temp,rescue,identity FROM rename_items WHERE job=? ORDER BY n");rows.bind(1,job);
        while(rows.next()) { Record r{rows.text(0),rows.text(1),rows.text(2),rows.text(3),rows.text(4)};if(records.size()>=1000||key(r.source.parent_path())!=key(r.target.parent_path())||key(r.source.parent_path())!=key(r.temp.parent_path())||(!r.rescue.empty()&&key(r.source.parent_path())!=key(r.rescue.parent_path()))) throw std::runtime_error("Invalid recovery paths");
            std::vector<std::filesystem::path> candidates{r.source,r.target,r.temp,r.rescue};sql::Statement history(db_,"SELECT path FROM rename_locations WHERE job=? AND n=?");history.bind(1,job);history.bind(2,int64_t(records.size()));while(history.next()) candidates.emplace_back(history.text(0));
            std::filesystem::path found;for(auto const& path:candidates) if(!path.empty()&&key(path.parent_path())==key(r.source.parent_path())) { try { if(identity(path)==r.id) { found=path;break; } }catch(...) {} }if(found.empty()||!moving.insert(key(found)).second) throw std::runtime_error("Original file no longer found");records.push_back(r);located.push_back(found);
        }
    }if(records.empty()) throw std::runtime_error("Empty recovery record");
    for(auto const& r:records) if(present(r.source)&&!moving.contains(key(r.source))) throw std::runtime_error("Original name occupied; every file preserved");
    if(records.size()==1 && key(records.front().source)!=key(records.front().target)) {
        auto const& r=records.front();
        if(key(located.front())!=key(r.source)) move(located.front(),r.source,r.id);
        sql::Statement state(db_,"UPDATE rename_jobs SET state='rolled_back' WHERE id=?");
        state.bind(1,job);state.next();return;
    }
    { sql::Transaction transaction(db_);for(size_t n=0;n<records.size();++n) { records[n].rescue=temporary(located[n]);sql::Statement update(db_,"INSERT OR IGNORE INTO rename_locations(job,n,path) VALUES(?,?,?)");update.bind(1,job);update.bind(2,int64_t(n));update.bind(3,records[n].rescue.wstring());update.next(); }sql::Statement state(db_,"UPDATE rename_jobs SET state='recovering' WHERE id=?");state.bind(1,job);state.next();transaction.commit(); }
    for(size_t n=0;n<records.size();++n) move(located[n],records[n].rescue,records[n].id);
    for(auto const& r:records) move(r.rescue,r.source,r.id);
    sql::Statement state(db_,"UPDATE rename_jobs SET state='rolled_back' WHERE id=?");state.bind(1,job);state.next();
}
void Journal::recover() { for(auto job:pending()) restore(job); }
void Journal::undo() {
    auto job=latest();if(!job) throw std::runtime_error("No rename to undo");std::vector<Pair> pairs;{ sql::Statement rows(db_,"SELECT target,source,identity FROM rename_items WHERE job=? ORDER BY n");rows.bind(1,job);while(rows.next()) pairs.push_back({rows.text(0),rows.text(1),rows.text(2)}); }apply(pairs,job);
}
bool rename_tests(std::filesystem::path const& root) {
    try {
        std::filesystem::create_directories(root);auto a=root/L"原文.txt",b=root/L"second.txt",c=root/L"renamed-01.txt";if(present(a)||present(b)||present(c)) return false;
        if(!write_utf8_atomic(a.c_str(),L"first")||!write_utf8_atomic(b.c_str(),L"second")) return false;scope_exit clean([&]{DeleteFileW(a.c_str());DeleteFileW(b.c_str());DeleteFileW(c.c_str());DeleteFileW((root/L"renamed-02.txt").c_str());});
        Journal journal(root/L"journal.db");auto plan=preview({a,b},L"renamed-{n:02}{ext}");if(plan.size()!=2||plan[0].target!=c) return false;journal.apply(plan);if(present(a)||!present(c)) return false;journal.undo();if(!present(a)||!present(b)||present(c)) return false;
        auto ida=identity(a),idb=identity(b);journal.apply({{a,b,ida},{b,a,idb}});if(identity(a)!=idb||identity(b)!=ida) return false;journal.undo();if(identity(a)!=ida||identity(b)!=idb) return false;
        journal.apply(preview({a,b},L"renamed-{n:02}{ext}"),0,1);if(journal.pending().empty()) return false;journal.recover();if(!journal.pending().empty()||identity(a)!=ida||identity(b)!=idb) return false;
        for(auto const& invalid:{L"CON{ext}",L"same.txt",L"../{name}",L"{evil}"}) { bool rejected=false;try { preview({a,b},invalid); }catch(...) { rejected=true; }if(!rejected) return false; }
        auto stale=preview({a},L"renamed-{n:02}{ext}");write_utf8_atomic(c.c_str(),L"external");bool collision=false;try { journal.apply(stale); }catch(...) { collision=true; }if(!collision||!present(a)||!present(c)) return false;
        auto link=root/L"sample.lnk", renamed=root/L"approved.lnk", replaced=root/L"replaced.lnk";
        if(!write_utf8_atomic(link.c_str(),L"isolated fixture"))return false;
        auto single=preview({link},L"approved{ext}");auto original=single.front().identity;
        journal.apply(single,0,1);
        if(present(link)||identity(renamed)!=original||journal.pending().size()!=1)return false;
        for(auto const& e:std::filesystem::directory_iterator(root))
            if(e.path().filename().wstring().starts_with(L".LightBlockNative-rename-"))return false;
        journal.recover();
        if(!journal.pending().empty()||present(renamed)||identity(link)!=original)return false;
        std::filesystem::rename(link,replaced);
        if(!write_utf8_atomic(link.c_str(),L"external replacement"))return false;
        try{journal.apply(single);return false;}catch(...){}
        return identity(replaced)==original&&identity(link)!=original&&!present(renamed)&&journal.pending().empty();
    }catch(...) { return false; }
}
}
