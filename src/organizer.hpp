#pragma once
#include "lb.h"
#include <chrono>
#include <functional>
#include <filesystem>
#include <unordered_map>
struct UiGroup;

namespace lb::organizer {
struct Entry {
    std::wstring path,name,group;
    uint64_t modified{};
    bool system{},loose{},external{};
    std::chrono::steady_clock::time_point due{};
    bool directory{};
    bool custom_name{},manual_loose{},positioned{};
    POINT position{};
};
struct Group { std::wstring id,name;COLORREF color{};bool automatic{}; };
class Model {
    std::filesystem::path directory_;
public:
    std::vector<Entry> entries;
    std::vector<Group> groups;
    explicit Model(std::filesystem::path directory):directory_(std::move(directory)) {}
    bool load();
    bool save() const;
    void initialize_groups();
    bool synchronize(std::vector<std::wstring> const& roots,bool defer_new);
    bool collect(std::wstring const& path,std::wstring const& group={});
    bool rename_entry(std::wstring const& path,std::wstring const& name);
    bool restore_entry(std::wstring const& path);
    bool position_entry(std::wstring const& path,POINT position);
    bool add_paths(std::vector<std::wstring> const& paths,std::wstring const& group);
    bool remove_group(std::wstring const& id);
    bool move_group(size_t from,size_t to);
    Group& create_group(std::wstring name);
    std::vector<size_t> view(int order,std::wstring const& query,std::wstring const& group={}) const;
    size_t collect_due(std::chrono::steady_clock::time_point now);
    std::wstring classify(std::wstring const& path,bool directory,bool system=false) const;
};
bool configure(bool preview=false);
bool has_session();
bool transition_tests();
bool shutdown();
void tick(UINT timer);
void desktop_changed();
void shell_restarted();
bool message(UINT message,WPARAM wp,LPARAM lp);
int lease_main(int argc,wchar_t **argv);
bool model_tests(std::wstring const& directory);
bool entry_tests(std::wstring const& directory);
bool interaction_tests(std::wstring const& directory);
bool surface_tests(std::wstring const& directory);
bool arrival_tests(std::wstring const& directory);
int settings_group(int index,UiGroup* out);
int settings_group_action(int action,wchar_t const* group,wchar_t const* argument,int value);
bool settings_group_tests(std::wstring const& directory);
}
