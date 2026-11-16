#pragma once
#include "lb.h"
#include "ui_api.h"
#include "capture_ui_api.h"
struct UiRecordingExports;
struct UiScrollExports;
namespace lb::frontend {
bool initialize(bool visible);
bool active();
int run();
void show(int page=-1);
void hide();
HWND preview_host();
bool note_open(wchar_t const* file,bool activate);
bool note_close(wchar_t const* file);
bool notes_shutdown(bool preserve);
void notes_manager();
bool snapshots(wchar_t const* directory);
UiCaptureExports const* capture_ui();
UiRecordingExports const* recording_ui();
UiScrollExports const* scroll_ui();
bool launcher_show();
bool show_tool(int action);
bool update_tests(wchar_t const* directory);
void update_exit_failed();
bool prepare_exit(bool closing);
bool capture_hidden();
}
int settings_describe(int id,UiSetting* out);
int settings_change(int id,int value);
void app_command(int id);
int app_session_end(int phase);
void app_delete_item(int index);
