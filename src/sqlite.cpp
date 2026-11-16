#include "sqlite.hpp"
#include <mutex>
#include <limits>
namespace lb::sql {
namespace {
struct sqlite3;struct sqlite3_stmt;
using Destroy=void(*)(void*);
struct ModuleDelete { void operator()(HINSTANCE__* module) const noexcept { if(module) FreeLibrary(module); } };
struct Api {
    std::unique_ptr<HINSTANCE__,ModuleDelete> module;
    int (*open)(char const*,sqlite3**,int,char const*){};
    int (*close)(sqlite3*){};
    int (*exec)(sqlite3*,char const*,int(*)(void*,int,char**,char**),void*,char**){};
    int (*prepare)(sqlite3*,char const*,int,sqlite3_stmt**,char const**){};
    int (*finalize)(sqlite3_stmt*){};
    int (*step)(sqlite3_stmt*){};
    int (*bind_int)(sqlite3_stmt*,int,int64_t){};
    int (*bind_text)(sqlite3_stmt*,int,char const*,int,Destroy){};
    int (*bind_blob)(sqlite3_stmt*,int,void const*,int,Destroy){};
    int64_t (*column_int)(sqlite3_stmt*,int){};
    unsigned char const* (*column_text)(sqlite3_stmt*,int){};
    void const* (*column_blob)(sqlite3_stmt*,int){};
    int (*column_bytes)(sqlite3_stmt*,int){};
    int (*busy_timeout)(sqlite3*,int){};
    Api():module(LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if(!module) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
#define LOAD(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(module.get(),name));if(!field) throw winrt::hresult_error(E_NOINTERFACE)
        LOAD(open,"sqlite3_open_v2");LOAD(close,"sqlite3_close_v2");LOAD(exec,"sqlite3_exec");LOAD(prepare,"sqlite3_prepare_v2");LOAD(finalize,"sqlite3_finalize");LOAD(step,"sqlite3_step");
        LOAD(bind_int,"sqlite3_bind_int64");LOAD(bind_text,"sqlite3_bind_text");LOAD(bind_blob,"sqlite3_bind_blob");LOAD(column_int,"sqlite3_column_int64");LOAD(column_text,"sqlite3_column_text");LOAD(column_blob,"sqlite3_column_blob");LOAD(column_bytes,"sqlite3_column_bytes");LOAD(busy_timeout,"sqlite3_busy_timeout");
#undef LOAD
    }
};
std::shared_ptr<Api> api() { static std::weak_ptr<Api> cached;static std::mutex mutex;std::lock_guard guard(mutex);auto result=cached.lock();if(!result) { result=std::make_shared<Api>();cached=result; }return result; }
void check(int result) { if(result!=0) throw std::runtime_error("SQLite operation failed ("+std::to_string(result)+")"); }
struct ConnectionDelete { std::shared_ptr<Api> api;void operator()(sqlite3* database) const noexcept { if(database) api->close(database); } };
struct StatementDelete { std::shared_ptr<Api> api;void operator()(sqlite3_stmt* statement) const noexcept { if(statement) api->finalize(statement); } };
Destroy transient() { return reinterpret_cast<Destroy>(intptr_t(-1)); }
}
std::string utf8(std::wstring const& text) {
    if(text.empty()) return {};if(text.size()>INT_MAX) throw std::length_error("Unicode string too long");int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);if(!size) throw std::runtime_error("Invalid Unicode");
    std::string result(size_t(size),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size,nullptr,nullptr);return result;
}
std::wstring wide(std::string_view text) {
    if(text.empty()) return {};if(text.size()>INT_MAX) throw std::length_error("UTF-8 string too long");int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(!size) throw std::runtime_error("Invalid UTF-8");
    std::wstring result(size_t(size),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size);return result;
}
struct Database::Impl {
    std::shared_ptr<Api> functions{api()};
    std::unique_ptr<sqlite3,ConnectionDelete> connection{nullptr,ConnectionDelete{functions}};
    explicit Impl(std::filesystem::path const& path) {
        sqlite3* opened=nullptr;int result=functions->open(utf8(path.wstring()).c_str(),&opened,0x10006,nullptr);connection.reset(opened);check(result);check(functions->busy_timeout(opened,500));
    }
};
struct Statement::Impl {
    std::shared_ptr<Api> functions;
    std::unique_ptr<sqlite3_stmt,StatementDelete> statement;
    Impl(Database::Impl &database,char const* sql):functions(database.functions),statement(nullptr,StatementDelete{functions}) { sqlite3_stmt* prepared=nullptr;int result=functions->prepare(database.connection.get(),sql,-1,&prepared,nullptr);statement.reset(prepared);check(result); }
};
Database::Database(std::filesystem::path const& path):impl_(std::make_unique<Impl>(path)) {
    { Statement identity(*this,"PRAGMA application_id");if(identity.next()) { auto id=identity.integer(0);if(id&&id!=0x4c42444e) throw winrt::hresult_error(E_ACCESSDENIED);if(!id) { Statement tables(*this,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");if(tables.next()&&tables.integer(0)) throw winrt::hresult_error(E_ACCESSDENIED); } } }
    exec("PRAGMA application_id=1279411278;PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;PRAGMA secure_delete=ON;PRAGMA wal_autocheckpoint=64;PRAGMA cache_size=-1024;PRAGMA temp_store=MEMORY");
}
Database::~Database()=default;
void Database::exec(char const* sql) { check(impl_->functions->exec(impl_->connection.get(),sql,nullptr,nullptr,nullptr)); }
Statement::Statement(Database &database,char const* sql):impl_(std::make_unique<Impl>(*database.impl_,sql)) {}
Statement::~Statement()=default;
void Statement::bind(int index,int64_t value) { check(impl_->functions->bind_int(impl_->statement.get(),index,value)); }
void Statement::bind(int index,std::wstring const& value) { auto bytes=utf8(value);check(impl_->functions->bind_text(impl_->statement.get(),index,bytes.data(),int(bytes.size()),transient())); }
void Statement::bind(int index,std::vector<BYTE> const& value) { if(value.size()>INT_MAX) throw std::length_error("Blob too large");check(impl_->functions->bind_blob(impl_->statement.get(),index,value.empty()?static_cast<void const*>(""):value.data(),int(value.size()),transient())); }
bool Statement::next() { int result=impl_->functions->step(impl_->statement.get());if(result==100) return true;if(result==101) return false;check(result);return false; }
int64_t Statement::integer(int column) const { return impl_->functions->column_int(impl_->statement.get(),column); }
std::wstring Statement::text(int column) const { auto data=impl_->functions->column_text(impl_->statement.get(),column);int size=impl_->functions->column_bytes(impl_->statement.get(),column);if(!data||!size) return {};return wide(std::string_view(reinterpret_cast<char const*>(data),size_t(size))); }
std::vector<BYTE> Statement::blob(int column) const { auto data=static_cast<BYTE const*>(impl_->functions->column_blob(impl_->statement.get(),column));int size=impl_->functions->column_bytes(impl_->statement.get(),column);if(size<0||size>32*1024*1024) throw std::length_error("Blob too large");if(!data||!size) return {};return {data,data+size}; }
bool tests(std::filesystem::path const& path) {
    try {
        Database database(path);database.exec("CREATE TABLE IF NOT EXISTS sample(id INTEGER PRIMARY KEY,name TEXT,payload BLOB);DELETE FROM sample");{ Transaction transaction(database);Statement insert(database,"INSERT INTO sample VALUES(?,?,?)");insert.bind(1,int64_t(1));insert.bind(2,L"中文 🌙 ' ; DROP TABLE sample");insert.bind(3,std::vector<BYTE>{0,1,0,255});insert.next();transaction.commit(); }
        { Transaction transaction(database);database.exec("INSERT INTO sample VALUES(2,'rollback',NULL)"); }
        Statement query(database,"SELECT name,payload FROM sample WHERE id=?");query.bind(1,int64_t(1));if(!query.next()||query.text(0)!=L"中文 🌙 ' ; DROP TABLE sample"||query.blob(1)!=std::vector<BYTE>{0,1,0,255}) return false;
        Statement count(database,"SELECT COUNT(*) FROM sample");return count.next()&&count.integer(0)==1;
    }catch(...) { return false; }
}
}
