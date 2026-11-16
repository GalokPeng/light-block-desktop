#include "organizer.hpp"
#include "json.h"
#include <filesystem>
#include <set>

namespace lb::organizer {
namespace {
std::wstring lower(std::wstring text) { if(!text.empty()) CharLowerBuffW(text.data(),DWORD(text.size()));return text; }
std::wstring quote(std::wstring_view text) {
    std::wstring out=L"\"";for(wchar_t c:text) { if(c==L'"'||c==L'\\') { out+=L'\\';out+=c; }else if(c==L'\n') out+=L"\\n";else if(c==L'\r') out+=L"\\r";else if(c==L'\t') out+=L"\\t";else if(c>=32) out+=c; }out+=L'"';return out;
}
std::wstring string(Json const& json,int object,char const* key) { wchar_t out[LB_PATH];return json_string(&json,json_property(&json,object,key),out,LB_PATH)?out:L""; }
bool absolute_file(std::wstring const& path) { return !path.empty() && !PathIsRelativeW(path.c_str()) && path.find_first_of(L"\r\n") == std::wstring::npos; }
bool known_group(std::vector<Group> const& groups,std::wstring const& id) { return std::any_of(groups.begin(),groups.end(),[&](auto const& group){return group.id==id;}); }
int system_priority(Entry const& entry) { if(entry.path==L"shell:MyComputerFolder") return 0;if(entry.path==L"shell:RecycleBinFolder") return 1;if(entry.path==L"ms-settings:") return 2;return 3; }
}
void Model::initialize_groups() {
    if(!groups.empty()) return;
    wchar_t const* ids[]={L"system",L"folders",L"shortcuts",L"documents",L"images",L"media",L"archives",L"other"};
    wchar_t const* zh[]={L"系统图标",L"文件夹",L"快捷方式",L"文档与表格",L"图片与设计",L"视频与音频",L"压缩包与安装包",L"其他文件"};
    wchar_t const* en[]={L"System",L"Folders",L"Shortcuts",L"Documents",L"Images",L"Media",L"Archives",L"Other"};
    COLORREF colors[]={RGB(203,183,247),RGB(166,228,202),RGB(154,200,255),RGB(245,209,142),RGB(239,181,197),RGB(154,200,255),RGB(245,209,142),RGB(203,183,247)};
    for(size_t n=0;n<8;++n) groups.push_back({ids[n],tr(zh[n],en[n]),colors[n],true});
}
std::wstring Model::classify(std::wstring const& path,bool directory,bool system) const {
    if(system) return known_group(groups,L"system")?L"system":L"other";if(directory) return known_group(groups,L"folders")?L"folders":L"other";
    auto extension=lower(std::filesystem::path(path).extension().wstring());
    std::pair<wchar_t const*,wchar_t const*> types[]={
        {L"shortcuts",L"|.lnk|.url|.appref-ms|.website|.exe|"},{L"documents",L"|.txt|.md|.pdf|.doc|.docx|.xls|.xlsx|.csv|.ppt|.pptx|.rtf|.odt|"},
        {L"images",L"|.png|.jpg|.jpeg|.webp|.gif|.bmp|.svg|.tif|.tiff|.ico|.psd|.ai|.heic|"},{L"media",L"|.mp4|.mkv|.mov|.avi|.webm|.m4v|.mp3|.wav|.flac|.m4a|.ogg|"},{L"archives",L"|.zip|.7z|.rar|.gz|.tar|.msi|.msix|.iso|"}
    };
    if(!extension.empty()) for(auto const& [id,list]:types) if(std::wstring_view(list).find(L"|"+extension+L"|")!=std::wstring_view::npos) return known_group(groups,id)?id:L"other";return L"other";
}
bool Model::load() {
    auto file=directory_/L"organizer.json";std::error_code error;if(!std::filesystem::exists(file,error)) { if(error) return false;initialize_groups();return true; }
    Json json;if(!json_file(&json,file.c_str(),4*1024*1024)||json_integer(&json,json_property(&json,0,"schema"),0)!=1) return false;
    int group_array=json_property(&json,0,"groups"),entry_array=json_property(&json,0,"entries");if(group_array<0||entry_array<0||json.tokens[group_array].type!=JSON_ARRAY||json.tokens[entry_array].type!=JSON_ARRAY) return false;
    std::vector<Group> next_groups;std::vector<Entry> next_entries;std::set<std::wstring> ids,paths;
    for(int n=group_array+1;n<json.tokens[group_array].next;n=json.tokens[n].next) {
        Group group{string(json,n,"id"),string(json,n,"name"),COLORREF(json_integer(&json,json_property(&json,n,"color"),RGB(166,228,202))),false};
        int automatic=json_property(&json,n,"automatic");group.automatic=automatic>=0&&json.tokens[automatic].type==JSON_TRUE;
        if(group.id.empty()||group.id.size()>100||group.name.size()>120||!ids.insert(group.id).second||group.color>0xffffff||next_groups.size()>=128) return false;next_groups.push_back(std::move(group));
    }
    for(int n=entry_array+1;n<json.tokens[entry_array].next;n=json.tokens[n].next) {
        Entry entry;entry.path=string(json,n,"path");entry.name=string(json,n,"name");entry.group=string(json,n,"group");
        int system=json_property(&json,n,"system"),external=json_property(&json,n,"external"),loose=json_property(&json,n,"loose");
        entry.system=system>=0&&json.tokens[system].type==JSON_TRUE;entry.external=external>=0&&json.tokens[external].type==JSON_TRUE;entry.loose=loose>=0&&json.tokens[loose].type==JSON_TRUE;
        auto boolean=[&](char const* key){int value=json_property(&json,n,key);return value>=0&&json.tokens[value].type==JSON_TRUE;};
        entry.custom_name=boolean("custom_name");entry.manual_loose=boolean("manual_loose")&&entry.loose;entry.positioned=boolean("positioned");entry.position={LONG(json_integer(&json,json_property(&json,n,"x"),0)),LONG(json_integer(&json,json_property(&json,n,"y"),0))};
        if(entry.system) { if(entry.path!=L"shell:MyComputerFolder"&&entry.path!=L"shell:RecycleBinFolder"&&entry.path!=L"ms-settings:") return false; }
        else if(!absolute_file(entry.path)) return false;
        if(!known_group(next_groups,entry.group)||!paths.insert(lower(entry.path)).second||next_entries.size()>=10000) return false;
        entry.due=std::chrono::steady_clock::now()+std::chrono::minutes(1);next_entries.push_back(std::move(entry));
    }
    groups=std::move(next_groups);entries=std::move(next_entries);initialize_groups();return true;
}
bool Model::save() const {
    std::wstring text=L"{\"schema\":1,\"groups\":[";bool first=true;
    for(auto const& group:groups) { if(!first) text+=L",";first=false;text+=L"{\"id\":"+quote(group.id)+L",\"name\":"+quote(group.name)+L",\"color\":"+std::to_wstring(group.color)+L",\"automatic\":"+(group.automatic?L"true":L"false")+L"}"; }
    text+=L"],\"entries\":[";first=true;
    for(auto const& entry:entries) { if(!first) text+=L",";first=false;text+=L"{\"path\":"+quote(entry.path)+L",\"name\":"+quote(entry.name)+L",\"group\":"+quote(entry.group)+L",\"system\":"+(entry.system?L"true":L"false")+L",\"loose\":"+(entry.loose?L"true":L"false")+L",\"external\":"+(entry.external?L"true":L"false")+L",\"custom_name\":"+(entry.custom_name?L"true":L"false")+L",\"manual_loose\":"+(entry.manual_loose?L"true":L"false")+L",\"positioned\":"+(entry.positioned?L"true":L"false")+L",\"x\":"+std::to_wstring(entry.position.x)+L",\"y\":"+std::to_wstring(entry.position.y)+L"}"; }
    text+=L"]}\n";return write_utf8_atomic((directory_/L"organizer.json").c_str(),text.c_str())!=FALSE;
}
bool Model::synchronize(std::vector<std::wstring> const& roots,bool defer_new) {
    initialize_groups();std::vector<Entry> found;std::unordered_map<std::wstring,size_t> previous;
    for(size_t n=0;n<entries.size();++n) previous.emplace(lower(entries[n].path),n);
    auto append=[&](std::wstring path,std::wstring name,bool directory,bool system,uint64_t modified) {
        auto existing=previous.find(lower(path));if(existing!=previous.end()) { Entry item=entries[existing->second];item.path=std::move(path);if(!item.custom_name)item.name=std::move(name);item.external=false;item.modified=modified;item.directory=directory;found.push_back(std::move(item));previous.erase(existing);return; }
        Entry item;item.path=std::move(path);item.name=std::move(name);item.group=classify(item.path,directory,system);item.modified=modified;item.system=system;item.directory=directory;item.loose=defer_new&&!system;item.due=std::chrono::steady_clock::now()+std::chrono::minutes(1);found.push_back(std::move(item));
    };
    append(L"shell:MyComputerFolder",tr(L"此电脑",L"This PC"),false,true,0);append(L"shell:RecycleBinFolder",tr(L"回收站",L"Recycle Bin"),false,true,0);append(L"ms-settings:",tr(L"Windows 设置",L"Windows settings"),false,true,0);
    for(auto const& root:roots) {
        auto pattern=std::filesystem::path(root)/L"*";WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW(pattern.c_str(),&data);if(find==INVALID_HANDLE_VALUE) { if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND) return false;continue; }
        scope_exit close([&]{FindClose(find);});
        do { if((data.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))||!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L"..")) continue;auto path=(std::filesystem::path(root)/data.cFileName).wstring();bool directory=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;std::wstring name=data.cFileName;if(!directory && lower(std::filesystem::path(name).extension().wstring())==L".lnk") name=std::filesystem::path(name).stem().wstring();append(std::move(path),std::move(name),directory,false,(uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime); }while(FindNextFileW(find,&data));
    }
    for(auto const& [path,index]:previous) if(entries[index].external) found.push_back(entries[index]);
    auto before=std::move(entries);entries=std::move(found);if(save())return true;entries=std::move(before);return false;
}
bool Model::collect(std::wstring const& path,std::wstring const& group) { for(auto &entry:entries) if(lower(entry.path)==lower(path)) { auto before=entry;if(!group.empty()) { if(!known_group(groups,group)) return false;entry.group=group; }entry.loose=false;entry.manual_loose=false;if(save())return true;entry=std::move(before);return false; }return false; }
bool Model::rename_entry(std::wstring const& path,std::wstring const& name) {
    if(name.size()>240||name.find_first_of(L"\r\n")!=std::wstring::npos)return false;
    for(auto& entry:entries)if(!_wcsicmp(entry.path.c_str(),path.c_str())){auto before=entry;entry.name=name;entry.custom_name=true;if(save())return true;entry=std::move(before);return false;}return false;
}
bool Model::restore_entry(std::wstring const& path) {
    auto found=std::find_if(entries.begin(),entries.end(),[&](auto const& entry){return !_wcsicmp(entry.path.c_str(),path.c_str());});if(found==entries.end())return false;
    auto before=entries;
    // These are references, never filesystem moves. Desktop items remain visible
    // through a loose icon while Explorer's icon layer is hidden. Explicit restore
    // is persistent and must not be undone by the one-minute collection timer.
    if(found->external&&!found->system)entries.erase(found);else {found->loose=true;found->manual_loose=true;}
    if(save())return true;entries=std::move(before);return false;
}
bool Model::position_entry(std::wstring const& path,POINT position) {
    for(auto& entry:entries)if(!_wcsicmp(entry.path.c_str(),path.c_str())){auto before=entry;entry.positioned=true;entry.position=position;if(save())return true;entry=std::move(before);return false;}return false;
}
bool Model::add_paths(std::vector<std::wstring> const& paths,std::wstring const& group) {
    if(paths.empty()||paths.size()>10000||!known_group(groups,group))return false;auto before=entries;
    for(auto const& path:paths){DWORD attributes=GetFileAttributesW(path.c_str());if(!absolute_file(path)||attributes==INVALID_FILE_ATTRIBUTES){entries=std::move(before);return false;}
        auto found=std::find_if(entries.begin(),entries.end(),[&](auto const& entry){return !_wcsicmp(entry.path.c_str(),path.c_str());});
        if(found!=entries.end()){found->group=group;found->loose=false;found->manual_loose=false;}
        else {if(entries.size()>=10000){entries=std::move(before);return false;}Entry item;item.path=path;item.name=std::filesystem::path(path).filename().wstring();item.group=group;item.external=true;item.directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;if(!item.directory&&lower(std::filesystem::path(path).extension().wstring())==L".lnk")item.name=std::filesystem::path(path).stem().wstring();entries.push_back(std::move(item));}
    }
    if(save())return true;entries=std::move(before);return false;
}
Group& Model::create_group(std::wstring name) { auto id=L"custom-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(groups.size());groups.push_back({std::move(id),std::move(name),RGB(166,228,202),false});return groups.back(); }
bool Model::remove_group(std::wstring const& id) { if(id==L"other") return false;auto group=std::find_if(groups.begin(),groups.end(),[&](auto const& item){return item.id==id;});if(group==groups.end()) return false;auto before=*this;for(auto &entry:entries) if(entry.group==id) entry.group=L"other";groups.erase(group);if(save())return true;*this=std::move(before);return false; }
bool Model::move_group(size_t from,size_t to) { if(from>=groups.size()||to>=groups.size()) return false;auto before=groups;auto group=std::move(groups[from]);groups.erase(groups.begin()+ptrdiff_t(from));groups.insert(groups.begin()+ptrdiff_t(to),std::move(group));if(save())return true;groups=std::move(before);return false; }
std::vector<size_t> Model::view(int order,std::wstring const& query,std::wstring const& group) const {
    std::vector<size_t> result;auto search=lower(query);
    for(size_t n=0;n<entries.size();++n) { auto const& item=entries[n];if(item.loose||(!group.empty()&&item.group!=group)) continue;if(!search.empty() && lower(item.name).find(search)==std::wstring::npos) { auto found=std::find_if(groups.begin(),groups.end(),[&](auto const& g){return g.id==item.group;});if(found==groups.end()||lower(found->name).find(search)==std::wstring::npos) continue; }result.push_back(n); }
    std::stable_sort(result.begin(),result.end(),[&](size_t a,size_t b) { auto const& x=entries[a];auto const& y=entries[b];int px=system_priority(x),py=system_priority(y);if(px!=py) return px<py;if(order==1&&x.modified!=y.modified) return x.modified>y.modified;return StrCmpLogicalW(x.name.c_str(),y.name.c_str())<0; });return result;
}
size_t Model::collect_due(std::chrono::steady_clock::time_point now) { std::vector<size_t> changed;for(size_t n=0;n<entries.size();++n)if(entries[n].loose&&!entries[n].manual_loose&&entries[n].due<=now){entries[n].loose=false;changed.push_back(n);}if(changed.empty()||save())return changed.size();for(auto n:changed)entries[n].loose=true;return 0; }
bool model_tests(std::wstring const& directory) {
    std::error_code error;std::filesystem::create_directories(directory,error);if(error) return false;Model model(directory);model.initialize_groups();
    if(model.classify(L"C:\\a.PDF",false)!=L"documents"||model.classify(L"C:\\a",true)!=L"folders") return false;
    model.entries={{L"C:\\test10.txt",L"test10",L"documents",10,false,false,false,{}},{L"shell:MyComputerFolder",L"此电脑",L"system",0,true,false,false,{}},{L"C:\\test2.txt",L"test2",L"documents",20,false,false,false,{}}};
    auto sorted=model.view(0,L"");if(sorted.size()!=3||sorted[0]!=1||sorted[1]!=2) return false;auto timed=model.view(1,L"");if(timed[0]!=1||timed[1]!=2) return false;
    auto custom=model.create_group(L"").id;if(!model.collect(model.entries[0].path,custom)||model.groups.back().name!=L"") return false;model.entries[0].loose=true;model.entries[0].due=std::chrono::steady_clock::now()-std::chrono::seconds(1);if(model.collect_due(std::chrono::steady_clock::now())!=1) return false;
    if(!model.save()) return false;Model restored(directory);return restored.load()&&restored.entries.size()==3&&restored.groups.back().name.empty()&&restored.entries[0].group==custom;
}
bool entry_tests(std::wstring const& directory) {
    std::error_code error;auto root=std::filesystem::path(directory),desktop=root/L"Desktop",outside=root/L"source";std::filesystem::create_directories(desktop,error);if(error)return false;std::filesystem::create_directories(outside,error);if(error)return false;
    auto document=desktop/L"source.txt",external=outside/L"shortcut-target.txt";if(!write_utf8_atomic(document.c_str(),L"never move or alter original")||!write_utf8_atomic(external.c_str(),L"external content"))return false;
    Model model(root);model.initialize_groups();if(!model.synchronize({desktop.wstring()},false))return false;auto group=model.create_group(L"Target").id;if(!model.save())return false;
    auto find=[&](std::wstring const& path)->Entry*{auto it=std::find_if(model.entries.begin(),model.entries.end(),[&](auto const& entry){return entry.path==path;});return it==model.entries.end()?nullptr:&*it;};
    if(!model.rename_entry(document.wstring(),L"显示名" )||!model.collect(document.wstring(),group)||!model.synchronize({desktop.wstring()},true))return false;
    auto entry=find(document.wstring());if(!entry||entry->name!=L"显示名"||entry->group!=group||!entry->custom_name)return false;
    if(!model.position_entry(document.wstring(),POINT{-200,300})||!model.restore_entry(document.wstring()))return false;
    if(model.collect_due(std::chrono::steady_clock::now()+std::chrono::hours(24))!=0||!model.synchronize({desktop.wstring()},true))return false;
    Model restored(root);if(!restored.load())return false;auto persisted=std::find_if(restored.entries.begin(),restored.entries.end(),[&](auto const& value){return value.path==document.wstring();});
    if(persisted==restored.entries.end()||!persisted->loose||!persisted->manual_loose||!persisted->custom_name||!persisted->positioned||persisted->position.x!=-200||persisted->position.y!=300)return false;
    if(!model.collect(document.wstring(),L"documents")||find(document.wstring())->manual_loose)return false;
    if(!model.add_paths({external.wstring()},group)||!find(external.wstring())||!find(external.wstring())->external)return false;
    if(!model.restore_entry(external.wstring())||find(external.wstring())||!std::filesystem::exists(external,error))return false;
    // Batch drop is all-or-nothing, including a late invalid path.
    auto count=model.entries.size();if(model.add_paths({external.wstring(),L"relative-invalid.txt"},group)||model.entries.size()!=count)return false;
    if(!model.restore_entry(L"shell:MyComputerFolder")||!model.synchronize({desktop.wstring()},true)||!find(L"shell:MyComputerFolder")->manual_loose)return false;
    auto first=model.groups[0].id,second=model.groups[1].id;if(!model.move_group(0,1)||model.groups[0].id!=second||model.groups[1].id!=first)return false;
    // A directory at the file's destination forces a real atomic-write failure.
    // No production paths, ACLs, Explorer windows, or desktop state are touched.
    auto blocked=root/L"blocked";std::filesystem::create_directories(blocked/L"organizer.json",error);if(error)return false;Model failed(blocked);failed.groups=model.groups;failed.entries=model.entries;
    auto old=failed.entries;auto old_groups=failed.groups;if(failed.rename_entry(document.wstring(),L"lost")||failed.collect(document.wstring(),group)||failed.restore_entry(document.wstring())||failed.position_entry(document.wstring(),POINT{9,9})||failed.add_paths({external.wstring()},group)||failed.move_group(0,1)||failed.remove_group(group)||failed.synchronize({desktop.wstring()},true))return false;
    if(failed.entries.size()!=old.size()||failed.groups.size()!=old_groups.size())return false;for(size_t n=0;n<old.size();++n)if(failed.entries[n].name!=old[n].name||failed.entries[n].group!=old[n].group||failed.entries[n].loose!=old[n].loose||failed.entries[n].position.x!=old[n].position.x)return false;
    for(size_t n=0;n<old_groups.size();++n)if(failed.groups[n].id!=old_groups[n].id)return false;
    failed.entries[0].loose=true;failed.entries[0].manual_loose=false;failed.entries[0].due={};if(failed.collect_due(std::chrono::steady_clock::now())||!failed.entries[0].loose)return false;
    HANDLE handle=CreateFileW(document.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)return false;unique_handle file(handle);char bytes[80]{};DWORD read{};return ReadFile(handle,bytes,sizeof bytes,&read,nullptr)&&std::string(bytes,read)=="never move or alter original";
}
}
