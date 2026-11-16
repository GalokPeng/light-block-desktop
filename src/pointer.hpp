#pragma once
#include "lb.h"
#include <filesystem>
namespace lb::pointer {
bool tint_cursor(std::vector<BYTE> &bytes,COLORREF color);
std::filesystem::path scheme(int theme,COLORREF color);
bool artwork_tests(std::filesystem::path const& directory);
bool configure();
bool has_session();
bool transition_tests();
bool shutdown();
void tick();
int helper_main(int argc,wchar_t **argv);
}
