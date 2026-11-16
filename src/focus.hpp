#pragma once
#include "lb.h"
#include <filesystem>
namespace lb::focus {
struct Timer {
    int stage{},rest_minutes{5};int64_t deadline{};
    void start(int64_t now,int minutes,int rest);
    bool advance(int64_t now);
    int64_t remaining(int64_t now) const { return stage?std::max<int64_t>(0,deadline-now):0; }
};
void show();void home();void layout();void restore();void shutdown();
HWND embed(HWND parent);
bool message(UINT message,WPARAM wp,LPARAM lp);
bool tests();
void const* ui_api();
bool service_tests(std::filesystem::path const& directory);
}
