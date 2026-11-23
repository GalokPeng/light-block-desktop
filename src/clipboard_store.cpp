#include "clipboard.hpp"
#include "network.h"
#include <optional>
namespace lb::clipboard {
namespace {
int64_t now() { FILETIME time;GetSystemTimeAsFileTime(&time);return int64_t(((uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000); }
int64_t scalar(sql::Database &database,char const* query) { sql::Statement statement(database,query);return statement.next()?statement.integer(0):0; }
}
Store::Store(std::filesystem::path const& path):database_(path) {
    database_.exec("CREATE TABLE IF NOT EXISTS clipboard(id INTEGER PRIMARY KEY,kind INTEGER NOT NULL,text TEXT NOT NULL,payload BLOB NOT NULL,digest TEXT NOT NULL UNIQUE,created INTEGER NOT NULL,pinned INTEGER NOT NULL DEFAULT 0);CREATE INDEX IF NOT EXISTS clip_order ON clipboard(pinned,created)");
}
bool Store::add(Item const& item) {
    if(item.kind!=0&&item.kind!=1) return false;if(item.kind&&item.image.size()>LB_CLIPBOARD_IMAGE_BYTES) return false;auto bytes=item.kind?item.image:std::vector<BYTE>{};if(!item.kind) { auto text=sql::utf8(item.text);bytes.assign(text.begin(),text.end()); }
    if(bytes.empty()||(!item.kind&&bytes.size()>5*1024*1024)) return false;bytes.insert(bytes.begin(),BYTE(item.kind));wchar_t digest[65];if(!hash_memory(bytes.data(),bytes.size(),L"SHA256",digest,65)) throw std::runtime_error("Clipboard digest failed");
    sql::Transaction transaction(database_);
    { sql::Statement insert(database_,"INSERT OR IGNORE INTO clipboard(kind,text,payload,digest,created) VALUES(?,?,?,?,?)");insert.bind(1,int64_t(item.kind));insert.bind(2,item.text);insert.bind(3,item.image);insert.bind(4,std::wstring(digest));insert.bind(5,now());insert.next(); }
    { sql::Statement update(database_,"UPDATE clipboard SET created=? WHERE digest=?");update.bind(1,now());update.bind(2,std::wstring(digest));update.next(); }
    database_.exec("DELETE FROM clipboard WHERE pinned=0 AND id NOT IN(SELECT id FROM clipboard ORDER BY pinned DESC,created DESC,id DESC LIMIT 80)");
    while(scalar(database_,"SELECT COALESCE(SUM(length(payload)+length(CAST(text AS BLOB))),0) FROM clipboard")>32*1024*1024) {
        if(!scalar(database_,"SELECT COUNT(*) FROM clipboard WHERE pinned=0")) break;
        database_.exec("DELETE FROM clipboard WHERE id=(SELECT id FROM clipboard WHERE pinned=0 ORDER BY created,id LIMIT 1)");
    }
    // Pinned entries can fill the budget. Roll this insertion and its evictions
    // back together if the new entry itself could not be retained.
    sql::Statement kept(database_,"SELECT 1 FROM clipboard WHERE digest=?");
    kept.bind(1,std::wstring(digest));if(!kept.next()) return false;
    transaction.commit();return true;
}
std::vector<Item> Store::list(std::wstring const& query) {
    sql::Statement statement(database_,"SELECT id,kind,substr(text,1,320),created,pinned FROM clipboard WHERE text LIKE ? ESCAPE '\\' ORDER BY pinned DESC,created DESC,id DESC LIMIT 100");std::wstring pattern=L"%";for(wchar_t ch:query.substr(0,512)) { if(ch==L'%'||ch==L'_'||ch==L'\\') pattern+=L'\\';pattern+=ch; }pattern+=L'%';statement.bind(1,pattern);std::vector<Item> rows;
    while(statement.next()) { Item item;item.id=statement.integer(0);item.kind=int(statement.integer(1));item.text=statement.text(2);item.created=statement.integer(3);item.pinned=statement.integer(4)!=0;rows.push_back(std::move(item)); }return rows;
}
std::optional<Item> Store::get(int64_t id) {
    sql::Statement statement(database_,"SELECT id,kind,text,created,pinned,payload FROM clipboard WHERE id=?");statement.bind(1,id);if(!statement.next()) return {};Item item;item.id=statement.integer(0);item.kind=int(statement.integer(1));item.text=statement.text(2);item.created=statement.integer(3);item.pinned=statement.integer(4)!=0;item.image=statement.blob(5);return item;
}
void Store::toggle_pin(int64_t id) {
    sql::Transaction transaction(database_);auto item=get(id);if(!item) return;
    if(!item->pinned&&scalar(database_,"SELECT COUNT(*) FROM clipboard WHERE pinned=1")>=20) throw std::runtime_error("Up to 20 pinned clipboard entries are allowed");
    sql::Statement statement(database_,"UPDATE clipboard SET pinned=1-pinned WHERE id=?");statement.bind(1,id);statement.next();transaction.commit();
}
void Store::remove(int64_t id) { sql::Statement statement(database_,"DELETE FROM clipboard WHERE id=?");statement.bind(1,id);statement.next(); }
void Store::clear() { database_.exec("DELETE FROM clipboard WHERE pinned=0"); }
bool tests(std::filesystem::path const& path) {
    try {
        Store store(path);store.clear();Item text;text.text=L"中文 clipboard unique text";if(!store.add(text)||!store.add(text)||store.list(L"clipboard").size()!=1) return false;
        auto items=store.list(L"");auto id=items[0].id;store.toggle_pin(id);for(int n=0;n<100;++n) { Item item;item.text=L"entry "+std::to_wstring(n);if(!store.add(item)) return false; }
        if(store.list(L"").size()!=80||!store.get(id)||!store.get(id)->pinned) return false;store.clear();if(store.list(L"").size()!=1) return false;
        store.toggle_pin(id);store.remove(id);if(!store.list(L"").empty()) return false;Item large;large.image.resize(5*1024*1024+1);large.kind=1;large.text=L"image";if(!store.add(large))return false;large.image.resize(LB_CLIPBOARD_IMAGE_BYTES+1);return !store.add(large);
    }catch(...) { return false; }
}
}
