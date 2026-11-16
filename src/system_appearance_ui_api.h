#pragma once
#include <stdint.h>
enum { LB_APPEARANCE_ABI=1 };
enum { LB_NIGHTLIGHT_AVAILABLE,LB_NIGHTLIGHT_OS,LB_NIGHTLIGHT_POLICY,LB_NIGHTLIGHT_REMOTE,LB_NIGHTLIGHT_DRIVER };
struct SystemAppearanceUiApi {uint32_t version,size;int (*night_light)();};
SystemAppearanceUiApi const* system_appearance_api();
