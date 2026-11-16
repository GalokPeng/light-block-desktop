#pragma once
#include <stdint.h>
#include <wchar.h>
enum { LB_STUDIO_UI_ABI=1 };
struct UiStudioState {
    uint32_t serial;
    int phase,progress,width,height;
    int64_t duration_ms;
    double fps;
    wchar_t source[4096],destination[4096],message[512];
};
struct UiStudioExport {
    int64_t first_ms,last_ms;
    int x,y,width,height,format,fps,gif_width,gif_fast;
    wchar_t destination[4096];
};
struct UiStudioApi {
    uint32_t version,size;
    void (*state)(UiStudioState*);
    int (*open)(wchar_t const*);
    int (*export_file)(UiStudioExport const*);
    void (*cancel)();
};
