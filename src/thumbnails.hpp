#pragma once
#include "lb.h"
namespace lb::thumbnails {
void start();
void request(std::wstring path);
void receive();
void prioritize(int const* indices,int count);
HBITMAP get(int index);
bool cache_tests();
bool shutdown();
}
