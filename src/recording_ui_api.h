#pragma once
#include <stdint.h>
#include <wchar.h>

// Plain data only: the native engine and Qt UI use separate C++ runtimes.
enum { LB_RECORDING_UI_ABI = 4 };
struct UiRecordingState {
    int phase, english, paused, stopping, progress_percent, stop_hotkey;
    int width, height, format, resolution, fps, quality, gif_fast;
    int64_t elapsed_ms, duration_ms, frames, first, last, position;
    double source_fps;
    wchar_t status[240];
    int editing_crop, system_audio, microphone, audio_merging;
    int has_audio, preview_muted;
    int can_undo,can_redo;
};
struct UiRecordingCommand {
    int action, value, dragging;
    int64_t first, last, position;
};
using UiRecordingCallback = void (*)(void*, UiRecordingCommand const*);
struct UiRecordingExports {
    uint32_t version, size;
    uintptr_t (*create)(uintptr_t owner, UiRecordingState const*, UiRecordingCallback, void* context);
    void (*update)(uintptr_t, UiRecordingState const*);
    void (*position)(uintptr_t, int left, int top, int right, int bottom, int visible);
    void (*destroy)(uintptr_t);
    int (*tests)(wchar_t const* directory);
};
using GetRecordingUiExports = UiRecordingExports const* (*)();
