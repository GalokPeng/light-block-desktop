#pragma once
#include <stdint.h>
#include <wchar.h>
/* Only plain data and C calling conventions cross the independently linked UI boundary. */
enum { LB_UI_ABI=4, LB_UI_PATH=4096 };
struct UiState { int dark,english,count,selected,paused,muted;uint32_t library_revision;wchar_t status[240],directory[LB_UI_PATH],data[LB_UI_PATH];int download_busy,download_percent,download_index,download_preview,download_error;uint32_t download_serial;wchar_t download_path[LB_UI_PATH];uint32_t thumbnail_revision; };
struct UiItem { int kind,builtin;uintptr_t thumbnail;wchar_t title[180],path[LB_UI_PATH]; };
struct UiSetting { int id,section,kind,value,low,high,choice_count,choice_values[24];wchar_t label[160],choices[800]; };
struct UiGroup { int color,count;wchar_t id[104],name[128]; };
struct UiApi {
    uint32_t version,size;
    void (*state)(UiState*);
    int (*item)(int,UiItem*);
    int (*setting)(int,UiSetting*);
    int (*change)(int,int);
    void (*action)(int);
    void (*preview)(int,uintptr_t);
    void (*import_file)(wchar_t const*);
    void (*delete_item)(int);
    uintptr_t (*tool)(int,uintptr_t);
    void (*tool_size)(int,int,int);
    void (*notes_changed)();
    void (*online)(int,int);
    void (*play_file)(wchar_t const*,uintptr_t);
    int (*group)(int,UiGroup*);
    int (*group_action)(int,wchar_t const*,wchar_t const*,int);
    void const* (*extension)(int);
    void (*select_item)(int);
    uintptr_t (*thumbnail)(int);
};
struct UiExports {
    uint32_t version,size;
    int (*initialize)(UiApi const*,int);
    int (*run)();
    void (*show)(int);
    void (*hide)();
    uintptr_t (*preview_host)();
    void (*note_open)(wchar_t const*,int);
    void (*note_close)(wchar_t const*);
    int (*notes_shutdown)(int);
    void (*notes_manager)();
    int (*snapshots)(wchar_t const*);
    void (*launcher_show)();
    void (*show_tool)(int);
    int (*update_tests)(wchar_t const*);
    void (*update_exit_failed)();
};
using GetUiExports=UiExports const* (*)();
