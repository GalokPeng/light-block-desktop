#pragma once
#include "sqlite.hpp"
namespace lb::files {
struct Pair { std::filesystem::path source,target;std::wstring identity; };
std::vector<Pair> preview(std::vector<std::filesystem::path> const& paths,std::wstring const& pattern);
class Journal {
    sql::Database db_;
    void restore(int64_t job);
public:
    explicit Journal(std::filesystem::path const& database);
    int64_t apply(std::vector<Pair> const& pairs,int64_t undo_of=0,int interrupt_after=-1);
    int64_t latest();
    std::vector<int64_t> pending();
    void undo();
    void recover();
};
bool rename_tests(std::filesystem::path const& root);
}
