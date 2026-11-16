#pragma once
#include <stdint.h>
#include <stddef.h>
#define LB_MEDIA_WORKER_ABI 1u
struct UiMediaSettings { int32_t fit,monitor,fullscreen_pause,battery_pause; };
struct UiMediaRect { int32_t x,y,width,height; };
struct UiMediaRequest {
    uint32_t version,size;
    wchar_t const* path;
    uintptr_t owner,parent,embed;
    uint32_t token;
    int32_t desktop,probe_milliseconds;
    UiMediaSettings settings;
    // Callbacks remain valid until run() returns. No ownership crosses the ABI.
    int (*attach)(uintptr_t,int);
    int (*geometry)(int,UiMediaRect*);
    int (*fullscreen)(uintptr_t,int);
    void (*settings_changed)(UiMediaSettings*);
    void (*log)(wchar_t const*);
};
struct UiMediaResult { uint32_t version,size;int32_t error,ready;uint64_t frames,elapsed_ms; };
struct UiMediaWorkerExports { uint32_t version,size;int (*run)(UiMediaRequest const*,UiMediaResult*); };
using GetMediaWorkerExports=UiMediaWorkerExports const* (*)();
