#pragma once
#include "lb.h"
#include "file_tools_ui_api.h"
#include <filesystem>
namespace lb::files {
void show();
void home();
void layout();
HWND embed(HWND parent);
bool message(UINT message,WPARAM wp,LPARAM lp);
bool shutdown();
bool lifecycle_tests();
FileToolsUiApi const* ui_api();
bool ui_service_tests(std::filesystem::path const& root);
}
