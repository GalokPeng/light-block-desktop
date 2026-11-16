#pragma once
#include <stdint.h>
#include <wchar.h>
enum { LB_LAUNCHER_UI_ABI=1, LB_LAUNCHER_ICON_SIZE=32 };
struct LauncherUiState { uint32_t revision;int count,indexing; };
struct LauncherUiResult {
    uint64_t id;
    int kind,command,folder; // kind: 0 file/app, 1 tool, 2 calculator
    wchar_t title[260],subtitle[160],target[4096];
};
struct LauncherUiApi {
    uint32_t version,size;
    void (*open)();
    void (*close)();
    void (*state)(LauncherUiState*);
    int (*query)(wchar_t const*,LauncherUiResult*,int capacity);
    int (*icon)(uint64_t id,uint32_t* premultiplied_argb,int capacity);
    int (*activate)(uint64_t id);
    int (*tests)();
};
LauncherUiApi const* launcher_ui_api();
