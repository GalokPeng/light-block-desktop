#pragma once
#include <stdint.h>
#include <wchar.h>
enum{LB_SCROLL_UI_ABI=1};
enum UiScrollCommand{ScrollFinish=1,ScrollCancel,ScrollNext,ScrollUndo,ScrollManual};
struct UiScrollState{
    int english,left,top,width,height,pages,frame_height,overlap,preview_height;
    int can_next,can_undo,can_manual,finishing;
    uint32_t revision;uintptr_t thumbnail;
    wchar_t status[256];
};
using UiScrollCallback=void(*)(uintptr_t owner,int command,int value);
struct UiScrollExports{
    uint32_t version,size;
    uintptr_t(*create)(uintptr_t owner,UiScrollCallback,UiScrollState const*);
    void(*update)(uintptr_t,UiScrollState const*);
    void(*show)(uintptr_t,int);
    void(*destroy)(uintptr_t);
    int(*tests)(wchar_t const*);
};
using GetScrollUiExports=UiScrollExports const*(*)();
