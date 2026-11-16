#pragma once
#include "lb.h"
#include <filesystem>

namespace lb::taskbar {
class RecoveryLease {
    struct Data;
    std::unique_ptr<Data> data_;
public:
    RecoveryLease();
    ~RecoveryLease();
    bool work(HWND bar,RECT before,RECT after);
    bool region(HWND bar,int previous_kind,RECT bounds);
    bool topmost(HWND bar);
    bool restore() noexcept;
};
bool recovery_pending();
bool recover_visibility() noexcept;
bool recovery_tests(std::wstring const& directory);
}
