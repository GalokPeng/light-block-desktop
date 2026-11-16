#pragma once
#include "lb.h"
#include <filesystem>
namespace lb::sql {
class Statement;
class Database {
    struct Impl;std::unique_ptr<Impl> impl_;
    friend class Statement;
public:
    explicit Database(std::filesystem::path const& path);
    ~Database();
    Database(Database const&)=delete;
    Database& operator=(Database const&)=delete;
    void exec(char const* sql);
};
class Statement {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Statement(Database &database,char const* sql);
    ~Statement();
    void bind(int index,int64_t value);
    void bind(int index,std::wstring const& value);
    void bind(int index,std::vector<BYTE> const& value);
    bool next();
    int64_t integer(int column) const;
    std::wstring text(int column) const;
    std::vector<BYTE> blob(int column) const;
};
class Transaction {
    Database &database_;bool active_{true};
public:
    explicit Transaction(Database &database):database_(database) { database_.exec("BEGIN IMMEDIATE"); }
    ~Transaction() noexcept { if(active_) try { database_.exec("ROLLBACK"); }catch(...) {} }
    void commit() { database_.exec("COMMIT");active_=false; }
};
std::string utf8(std::wstring const& text);
std::wstring wide(std::string_view text);
bool tests(std::filesystem::path const& path);
}
