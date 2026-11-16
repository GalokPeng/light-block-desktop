#ifndef LB_H
#define LB_H
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include <strsafe.h>
#include "native.hpp"

#define LB_PATH 4096
#define LB_MAX_ITEMS 10006
#include "version.hpp"
#define LB_CLASS L"LightBlockDesktopNative.Main.v1"
#define LB_PLAYER L"LightBlockDesktopNative.Player.v1"
#define WM_LB_HELLO (WM_APP+10)
#define WM_LB_READY (WM_APP+11)
#define WM_LB_ERROR (WM_APP+12)
#define WM_LB_IMPORT (WM_APP+13)
#define WM_LB_VIDEO (WM_APP+14)
#define WM_LB_TRAY (WM_APP+15)
#define WM_LB_CONTROL (WM_APP+16)
#define WM_LB_POSITION (WM_APP+17)
#define WM_LB_DURATION (WM_APP+18)
#define WM_LB_STATE (WM_APP+19)
#define LB_MIN(a,b) ((a)<(b)?(a):(b))
#define LB_MAX(a,b) ((a)>(b)?(a):(b))

typedef enum { MEDIA_IMAGE, MEDIA_GIF, MEDIA_VIDEO, MEDIA_UNKNOWN } MediaKind;
struct Settings {
    int fit,monitor,fullscreen_pause,battery_pause,restore_on_start;
    int organizer_enabled,organizer_alignment,organizer_view,organizer_auto_collect;
    int organizer_views[3]{};
    int organizer_icon_size,organizer_opacity,organizer_glass;
    int organizer_custom_position,organizer_x,organizer_y;
    int taskbar_enabled,taskbar_radius,taskbar_opacity;
    int cursor_theme,cursor_scale,cursor_particles,particle_style;
    int capture_enabled,capture_modifiers,capture_key,record_resolution,record_fps,record_lossless;
    int clipboard_enabled,notes_background,hotkeys_enabled,wallpaper_sound;
    int record_system_audio,record_microphone;
    COLORREF organizer_color,icon_color,taskbar_color,cursor_color,particle_color;
    wchar_t active_path[LB_PATH];
    wchar_t capture_directory[LB_PATH];
};
struct Image {
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    UINT width{},height{},count{},index{},delay{},disposal{};
    uint32_t background{};
    RECT previous{};
    std::unique_ptr<uint32_t[]> pixels,backup,scratch;
    size_t bytes{},scratch_capacity{};
    BOOL gif{};
};
struct Item { wchar_t path[LB_PATH], title[160]; MediaKind kind; HBITMAP thumb;int builtin; };
struct Player {
    lb::unique_handle process,job;
    DWORD pid, token;
    HWND window;
    HWND embed{};
    int64_t position{},duration{};
    BOOL muted{TRUE},paused{FALSE};
    ULONGLONG started, closing;
    BOOL ready, pending, pending_desktop, quitting, failed;
    wchar_t pending_path[LB_PATH];
};
struct App {
    HINSTANCE instance;
    HWND window;
    wchar_t directory[LB_PATH], data[LB_PATH], config[LB_PATH], exe[LB_PATH];
    BOOL dark, english, muted, paused, close_requested;
    int language;
    UINT dpi, page, filter, selected, count;
    std::vector<Item> items;
    uint32_t library_revision{1};
    uint32_t thumbnail_revision{1};
    HFONT font, small, heading, title;
    Player player;
    Player wallpaper;
    Settings settings;
    wchar_t status[240];
    NOTIFYICONDATAW tray;
    HANDLE mutex, import_thread;
    int hovered;
    ULONGLONG heartbeat, max_gap;
};
extern App g_app;

const wchar_t *tr(const wchar_t *zh, const wchar_t *en);
BOOL path_join(wchar_t *dst, size_t cap, const wchar_t *dir, const wchar_t *name);
BOOL ensure_directory(const wchar_t *path);
BOOL quote_arg(wchar_t *dst, size_t cap, const wchar_t *arg);
MediaKind media_kind(const wchar_t *path);
void log_line(const wchar_t *format, ...);
void set_status(const wchar_t *zh, const wchar_t *en);
void error_box(HWND owner, const wchar_t *action, HRESULT hr);
BOOL write_utf8_atomic(const wchar_t *path, const wchar_t *text);
wchar_t *read_utf8_file(const wchar_t *path,DWORD limit);
BOOL save_config(void);
void load_config(void);
void theme_window(HWND window);
void draw_text(HDC dc, const wchar_t *text, RECT rect, HFONT font, COLORREF color, UINT flags);
void rounded(HDC dc, RECT r, int radius, COLORREF fill, COLORREF border);
RECT fit_rect(RECT bounds, UINT width, UINT height, BOOL fill);

HRESULT image_open(Image *image, const wchar_t *path);
HRESULT image_next(Image *image);
void image_close(Image *image);
void image_draw(HDC dc, const Image *image, RECT bounds, int mode);
HBITMAP image_thumbnail(const wchar_t *path, UINT width, UINT height);
BOOL save_png(HBITMAP bitmap, const wchar_t *path,bool alpha=false);

void player_request(const wchar_t *path, BOOL desktop);
void player_stop(BOOL clear_pending);
void player_restore_wallpaper(void);
void player_tick(void);
void player_message(UINT message, WPARAM token, LPARAM value);
void player_control(int command);
void player_dispose(void);
void player_preview_embed(const wchar_t *path,HWND host);
void player_stop_preview(void);
Player* player_result_state(HWND host);
BOOL player_result_request(wchar_t const* path,HWND host,HWND owner);
void player_result_stop(HWND host);
size_t player_result_slots();
bool player_result_releasing();
bool player_result_tests();
void player_forget(const wchar_t *path);
bool player_uses_file(wchar_t const* path);
int player_file_released(const wchar_t *path);
void player_cancel_delete(const wchar_t *path);
bool player_release_tests(const wchar_t *directory);
void player_test_secondary(const wchar_t *path);
void player_settings_changed(void);
void player_refresh_settings(void);
void player_pause_desktop(bool paused);
Player& player_active(void);
int worker_main(int argc, wchar_t **argv);
HWND desktop_parent(void);
BOOL desktop_attach(HWND window, HMONITOR monitor);
BOOL fullscreen_on(HMONITOR monitor, HWND own);

void note_create(HWND owner);
BOOL notes_shutdown(void);
void notes_restore(void);
void notes_manager(void);
void notes_tick(void);
void note_open_saved(const wchar_t *file,BOOL activate);
void note_close_saved(const wchar_t *file);
void capture_start(HWND owner,bool record=false);
void capture_shutdown(void);
void capture_pin_bitmap(HBITMAP bitmap,RECT location,bool fit,bool edit=false,bool wheel_sizes=true);
void launcher_show(HWND owner);
void launcher_shutdown(void);
int run_self_tests(const wchar_t *report);
void stress_begin(const wchar_t *report,BOOL crash);
void stress_tick(void);
void settings_show(int page);
void app_open_settings(int page);
void settings_mount(int page);
void settings_layout();
void settings_refresh();
bool settings_dialog_message(MSG *message);
void settings_sync_clipboard();
bool settings_navigation_test();
bool settings_patch_test();
void settings_close(void);
BOOL configure_hotkey(UINT modifiers,UINT key);
bool valid_capture_hotkey(UINT modifiers,UINT key);
BOOL configure_tool_hotkeys(bool enabled,bool recording);
bool tool_hotkey_registered(int id);
int settings_wallpaper_sound(bool enabled);
int settings_recording_audio(bool microphone,bool enabled);
BOOL startup_set(BOOL enabled);
BOOL startup_enabled(void);
BOOL night_light_supported(void);
void online_show(void);
BOOL online_shutdown(void);
bool online_message(UINT message,WPARAM wp,LPARAM lp);
void online_start(int index,int preview);
UINT online_catalog_test(void);
void library_import(const wchar_t *path);
void library_import_checked(const wchar_t *path,const wchar_t *sha1);
void library_import_apply_checked(const wchar_t *path,const wchar_t *sha1);

#endif
