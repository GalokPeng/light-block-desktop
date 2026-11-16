#include "frontend.hpp"
#include "thumbnails.hpp"
#include "clipboard.hpp"
#include "file_tools.hpp"
#include "focus.hpp"
#include "network.h"
#include "recording_ui_api.h"
#include "scroll_ui_api.h"
#include "recording.hpp"
#include "online_catalog_ui_api.h"
#include "system_appearance_ui_api.h"
#include "organizer.hpp"
#include "launcher_ui_api.h"
void online_snapshot(UiState*);
namespace lb::frontend {
namespace {
HMODULE module{};UiExports const* exports{};UiApi api{};
void read_state(UiState* out) {
    if(!out) return;*out={};out->dark=g_app.dark;out->english=g_app.english;out->count=int(g_app.count);out->selected=int(g_app.selected);out->paused=g_app.paused;out->muted=g_app.muted;StringCchCopyW(out->status,240,g_app.status);StringCchCopyW(out->directory,LB_UI_PATH,g_app.directory);StringCchCopyW(out->data,LB_UI_PATH,g_app.data);online_snapshot(out);
    out->library_revision=g_app.library_revision;out->thumbnail_revision=g_app.thumbnail_revision;
}
int read_item(int n,UiItem* out) {
    if(!out||n<0||n>=int(g_app.count)) return 0;*out={};auto const& item=g_app.items[n];out->kind=int(item.kind);out->builtin=item.builtin;out->thumbnail=uintptr_t(item.thumb);StringCchCopyW(out->path,LB_UI_PATH,item.path);
    wchar_t const* zh[]={L"夜驰 · 城郊高速跑车",L"银河 · 高山湖泊星空",L"山海 · 晨光与海湾",L"海岸 · 雨夜弯道",L"星轨弧光",L"光的呼吸"};wchar_t const* en[]={L"Night drive",L"Milky Way",L"Mountain bay",L"Rain coast",L"Star trail · GIF",L"Motion · Video"};StringCchCopyW(out->title,180,item.builtin>=0&&item.builtin<6?tr(zh[item.builtin],en[item.builtin]):item.title);return 1;
}
uintptr_t thumbnail(int index) {return uintptr_t(thumbnails::get(index));}
void select_item(int index) { if(index>=0&&index<int(g_app.count)) g_app.selected=UINT(index); }
void preview(int index,uintptr_t host) { if(index<0) { player_stop_preview();return; }if(index>=int(g_app.count)||!IsWindow(HWND(host))) return;g_app.selected=UINT(index);player_preview_embed(g_app.items[index].path,HWND(host)); }
uintptr_t tool(int action,uintptr_t host) { if(!IsWindow(HWND(host))) return 0;switch(action) { case 707:return uintptr_t(clipboard::embed(HWND(host)));case 708:return uintptr_t(files::embed(HWND(host)));case 709:return uintptr_t(focus::embed(HWND(host))); }return 0; }
void tool_size(int action,int,int) { switch(action) {case 707:clipboard::layout();break;case 708:files::layout();break;case 709:focus::layout();break;} }
void play_file(wchar_t const* path,uintptr_t host) { if(path&&IsWindow(HWND(host))) player_preview_embed(path,HWND(host)); }
void const* extension(int id) {switch(id){case 1:return files::ui_api();case 2:return clipboard::ui_api();case 3:return focus::ui_api();case 4:return launcher_ui_api();case 5:return recording::studio_api();case 6:return online_catalog_api();case 7:return system_appearance_api();default:return nullptr;}}
}
bool initialize(bool visible) {
    std::unique_ptr<wchar_t,decltype(&free)> name(read_utf8_file((std::filesystem::path(g_app.directory)/L"ui-current.txt").c_str(),256),free);std::wstring filename=name?name.get():L"LightBlockUi.dll";while(!filename.empty()&&iswspace(filename.back())) filename.pop_back();if(!safe_filename(filename.c_str())||!filename.starts_with(L"LightBlockUi")||!filename.ends_with(L".dll")) return false;
    auto path=std::filesystem::path(g_app.directory)/filename;module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module) { log_line(L"C++ Qt frontend unavailable: %lu",GetLastError());return false; }
    auto entry=reinterpret_cast<GetUiExports>(GetProcAddress(module,"LightBlockUiExports"));if(!entry) return false;auto value=entry();if(!value||value->version!=LB_UI_ABI||value->size!=sizeof(UiExports)) return false;
    api={LB_UI_ABI,sizeof(UiApi),read_state,read_item,settings_describe,settings_change,app_command,preview,library_import,app_delete_item,tool,tool_size,notes_tick,online_start,play_file,organizer::settings_group,organizer::settings_group_action,extension,select_item,thumbnail};
    if(!value->initialize(&api,visible)) return false;exports=value;return true;
}
bool active() { return exports!=nullptr; }int run() { return exports?exports->run():-1; }
void show(int page) { if(exports) exports->show(page); }void hide() { if(exports) exports->hide(); }
HWND preview_host() { return exports?HWND(exports->preview_host()):nullptr; }
bool note_open(wchar_t const* file,bool activate) { if(!exports) return false;exports->note_open(file,activate);return true; }
bool note_close(wchar_t const* file) { if(!exports) return false;exports->note_close(file);return true; }
bool notes_shutdown(bool preserve) { return !exports||exports->notes_shutdown(preserve)!=0; }
void notes_manager() { if(exports) exports->notes_manager(); }
bool snapshots(wchar_t const* directory) { return exports&&exports->snapshots(directory)!=0; }
UiCaptureExports const* capture_ui() { if(!exports)return nullptr;auto entry=reinterpret_cast<GetCaptureUiExports>(GetProcAddress(module,"LightBlockCaptureUiExports"));auto value=entry?entry():nullptr;return value&&value->version==LB_CAPTURE_UI_ABI&&value->size==sizeof(UiCaptureExports)?value:nullptr; }
UiRecordingExports const* recording_ui() { if(!exports)return nullptr;auto entry=reinterpret_cast<GetRecordingUiExports>(GetProcAddress(module,"LightBlockRecordingUiExports"));auto value=entry?entry():nullptr;return value&&value->version==LB_RECORDING_UI_ABI&&value->size==sizeof(UiRecordingExports)?value:nullptr; }
UiScrollExports const* scroll_ui() { if(!exports)return nullptr;auto entry=reinterpret_cast<GetScrollUiExports>(GetProcAddress(module,"LightBlockScrollUiExports"));auto value=entry?entry():nullptr;return value&&value->version==LB_SCROLL_UI_ABI&&value->size==sizeof(UiScrollExports)?value:nullptr; }
bool launcher_show() {if(!exports||!exports->launcher_show)return false;exports->launcher_show();return true;}
bool show_tool(int action) {if(!exports||!exports->show_tool)return false;exports->show_tool(action);return true;}
bool update_tests(wchar_t const* directory) {return exports&&exports->update_tests(directory)!=0;}
void update_exit_failed() {if(exports)exports->update_exit_failed();}

}
