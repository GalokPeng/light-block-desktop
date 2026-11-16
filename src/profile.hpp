#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace lb::profile {
struct Selection { bool ok{},migrated{};std::filesystem::path path;std::wstring error; };
// legacy_override exists solely to support isolated tests/portable deployments.
// An explicit data directory never falls back to another user's profile.
Selection select(std::filesystem::path const& executable,
                 std::filesystem::path const& custom_directory={},
                 std::filesystem::path const& legacy_override={}) noexcept;
Selection select_with_progress(std::filesystem::path const& executable,
                               std::filesystem::path const& custom_directory={}) noexcept;
struct MenuEntry { std::wstring key,label,icon,command; };
std::vector<MenuEntry> desktop_menu_entries(std::filesystem::path const& executable,bool english,std::filesystem::path const& profile={},bool muted=true);
bool is_installed(std::filesystem::path const& executable) noexcept;
// Only interactive preview launches may register the separate preview keys.
// Automated fixtures and snapshots must pass official=false.
bool desktop_menu(std::filesystem::path const& executable,bool enabled,bool official,bool english=false,std::filesystem::path const& profile={},bool muted=true) noexcept;
bool tests(std::filesystem::path const& isolated_directory);
}
