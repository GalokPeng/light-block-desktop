#pragma once
#include <stdint.h>
#include <wchar.h>
// Plain values only: Qt/GCC and the native engine use independent C++ runtimes.
enum { LB_CAPTURE_UI_ABI=4 };
enum UiCaptureCommand {
    CaptureSelectTool=1,CaptureUndo,CaptureRedo,CapturePin,CaptureSave,CaptureCopy,
    CaptureClose,CaptureRecord,CaptureLongshot,CaptureReset,CaptureFull,CaptureSmart,
    CaptureColor=100,CaptureWidth,CaptureRadius,CaptureFill,CaptureDash,CaptureFont,
    CaptureFontSize,CaptureBold,CaptureItalic,CaptureNumber,CaptureBlockSize,CaptureUnderline,
    CaptureCrop,CaptureCropApply,CaptureCropCancel
};
struct UiCaptureState {
    int english,mode,tool,smart,can_undo,can_redo;
    int color,width,radius,filled,dashed,font_size,bold,italic,number,block_size;
    int left,top,right,bottom;
    wchar_t font[64];
    int underline;
    int cropping;
};
using UiCaptureCallback=void (*)(uintptr_t owner,int command,int value,wchar_t const* text);
struct UiCaptureExports {
    uint32_t version,size;
    uintptr_t (*create)(uintptr_t owner,UiCaptureCallback,UiCaptureState const*);
    void (*update)(uintptr_t handle,UiCaptureState const*);
    void (*destroy)(uintptr_t handle);
    int (*owns_focus)(uintptr_t handle,uintptr_t focus);
    int (*tests)(wchar_t const* directory);
    int (*pin_menu)(uintptr_t owner,int flags,int physical_x,int physical_y);
};
using GetCaptureUiExports=UiCaptureExports const* (*)();
