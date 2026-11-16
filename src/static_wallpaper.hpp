#pragma once
#include "lb.h"

namespace lb::static_wallpaper {
struct Outcome { bool completed{},success{},restored{},cancelled{};HRESULT error{};std::wstring path; };
// All file decoding, registry writes and blocking shell broadcasts happen in
// a bounded child process. tick() never waits for that process.
bool request(wchar_t const* path);
bool restore();
bool busy() noexcept;
bool reading(wchar_t const* path) noexcept;
bool has_original() noexcept;
void cancel() noexcept;
Outcome tick();
void dispose() noexcept;
int helper_main(int argc,wchar_t** argv);
bool tests(wchar_t const* directory);
}
