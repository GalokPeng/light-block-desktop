#pragma once
#include <stdint.h>
#include <wchar.h>
enum { LB_FOCUS_UI_ABI=1 };
struct UiFocusState { int stage,minutes,rest,pause;int64_t seconds,rounds,completed_seconds;wchar_t status[300]; };
struct UiWorkspace { int64_t id;wchar_t name[180],description[500]; };
struct UiFocusApi {
    uint32_t version,size;
    int (*read)(UiFocusState*);
    int (*workspace)(int,UiWorkspace*);
    // 0 start, 1 stop, 2 save workspace, 3 apply workspace, 4 remove workspace.
    int (*action)(int,int64_t,wchar_t const*,int,int,int);
};
