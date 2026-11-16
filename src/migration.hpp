#pragma once
#include <filesystem>
#include <string>
namespace lb::migration {
struct Result { bool ok{}, migrated{}; std::wstring error; };
Result prepare_legacy_source(std::filesystem::path const& source,
                             std::filesystem::path const& expected_executable,
                             bool request_quit) noexcept;
// Run before loading settings or starting any desktop services. Source is never
// modified. Destination must be absent, or contain our completed migration marker.
Result upgrade(std::filesystem::path const& source, std::filesystem::path const& destination,
               std::filesystem::path const& assets) noexcept;
bool tests(std::filesystem::path const& isolated_directory);
}
