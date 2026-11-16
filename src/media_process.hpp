#pragma once
#include "lb.h"
#include <filesystem>
#include <functional>

namespace lb::media {
struct Progress { int64_t microseconds{},frames{}; };
struct Result { HRESULT error{E_FAIL};DWORD exit_code{};Progress progress; };
struct Control { std::atomic_bool cancel{},stop{};DWORD timeout_ms{};std::function<void()> started,heartbeat; };
std::wstring runtime_path();
Result run(std::vector<std::wstring> const& arguments,std::filesystem::path const& log,Control &control,std::function<void(Progress)> progress={});
}
