#pragma once
#include "lb.h"
#include <optional>
namespace lb {
std::optional<std::wstring> calculate(std::wstring expression);
bool calculator_tests();
}
