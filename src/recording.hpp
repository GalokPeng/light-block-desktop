#pragma once
#include "lb.h"
#include "media_studio_ui_api.h"
namespace lb::recording {
bool start(RECT screen);
bool active();
void open_file(HWND owner);
void toggle(HWND owner);
bool shutdown();
void tick();
bool export_test(std::wstring const& directory);
bool dialog_tests();
bool crop_controls_tests();
bool parity_tests();
bool multi_result_tests();
UiStudioApi const* studio_api();
}
