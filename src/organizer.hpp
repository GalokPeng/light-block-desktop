#pragma once
#include "lb.h"
#include <chrono>
#include <optional>
#include <atomic>
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
    int pin{};
};
struct Group { std::wstring id,name;COLORREF color{};bool automatic{};std::wstring patterns; };
struct RootListing {
    std::wstring path;
    DWORD error{};
    std::vector<Entry> files;
};
std::vector<RootListing> scan_roots(std::vector<std::wstring> const& roots,std::atomic_bool const* cancelled=nullptr);
class Model {
    std::filesystem::path directory_;
    std::optional<Entry> undo_entry_;
    bool undo_removed_{};
public:
    std::vector<Entry> entries;
    std::vector<Group> groups;
    explicit Model(std::filesystem::path directory):directory_(std::move(directory)) {}
    bool load();
    uint64_t revision{};
    bool save();
    bool apply_scan(std::vector<RootListing> const& roots,bool defer_new,bool protect_empty_roots);
    void initialize_groups();
    bool synchronize(std::vector<std::wstring> const& roots,bool defer_new,bool protect_empty_roots=false);
    bool set_pin(std::wstring const& path,int position);
    bool set_patterns(std::wstring const& group,std::wstring patterns);
    bool can_undo()const{return undo_entry_.has_value();}
    bool undo();
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
bool visibility_tests(std::filesystem::path const& directory);
bool shutdown();
void suspend_for_session_end();
void resume_after_cancelled_session();
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
