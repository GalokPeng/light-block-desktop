#pragma once
#include <stdint.h>
#include <wchar.h>

/* Extension 2. Call on the UI thread; database/clipboard work runs on its worker.
   Caller owns every buffer. No C++ allocation or handle crosses the DLL boundary. */
enum { LB_CLIPBOARD_UI_ABI=1, LB_CLIPBOARD_UI_EXTENSION=2 };
enum { LB_CLIPBOARD_IMAGE_BYTES=16*1024*1024, LB_CLIPBOARD_IMAGE_PIXELS=32*1024*1024 };
enum ClipboardUiAction { ClipboardEnable=1, ClipboardCapture, ClipboardCopy,
    ClipboardPinImage, ClipboardTogglePin, ClipboardDelete, ClipboardClear };
struct ClipboardUiState {
    uint32_t revision,detail_revision;int enabled,count,busy,detail_busy,error;
    int64_t selected,detail_id;int detail_kind,text_chars,image_bytes;
    wchar_t status[240];
};
/* created is Unix epoch milliseconds, independent of the native store's FILETIME epoch. */
struct ClipboardUiItem { int64_t id,created;int kind,pinned;wchar_t summary[321]; };
struct ClipboardUiApi {
    uint32_t version,size;
    void (*state)(ClipboardUiState*);
    int (*item)(int,ClipboardUiItem*);
    void (*query)(wchar_t const*);
    void (*select)(int64_t);
    void (*action)(int,int64_t,int);
    int (*text)(wchar_t*,int);
    int (*image)(void*,int);
    void (*active)(int);
};
