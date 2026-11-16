#include "pointer.hpp"
#include "json.h"
#include <array>
namespace lb::pointer {
namespace {
unique_handle process,stop,changed;bool closing{};int recovery_attempts{};
std::filesystem::path receipt() { return std::filesystem::path(g_app.data)/L"native-pointer-session.json"; }
std::wstring event_name(wchar_t const* suffix,DWORD pid) { return L"Local\\LightBlockNative.Pointer."+std::wstring(suffix)+L"."+std::to_wstring(pid); }
struct SchemeLease {
    bool owned{};
    bool restore() noexcept {
        if(!owned) return true;if(!SystemParametersInfoW(SPI_SETCURSORS,0,nullptr,0)) return false;
        std::error_code error;std::filesystem::remove(receipt(),error);if(error) return false;owned=false;return true;
    }
    ~SchemeLease() { if(!restore()) log_line(L"pointer restoration remains pending"); }
    void recover() {
        if(GetFileAttributesW(receipt().c_str())==INVALID_FILE_ATTRIBUTES) return;Json json;
        if(!json_file(&json,receipt().c_str(),1024)||json_integer(&json,json_property(&json,0,"schema"),0)!=1) throw winrt::hresult_error(E_INVALIDARG);
        int id=json_property(&json,0,"native_pointer");if(id<0||json.tokens[id].type!=JSON_TRUE) throw winrt::hresult_error(E_INVALIDARG);
        owned=true;if(!restore()) throw winrt::hresult_error(E_FAIL);
    }
    void apply() {
        auto directory=scheme(g_app.settings.cursor_theme,g_app.settings.cursor_color);if(directory.empty()) throw winrt::hresult_error(E_INVALIDARG);
        struct Role { wchar_t const* name;DWORD id;bool optional; };
        Role roles[]={{L"normal",32512,false},{L"text",32513,false},{L"busy",32514,false},{L"cross",32515,false},{L"alternate",32516,false},
            {L"diagonal1",32642,false},{L"diagonal2",32643,false},{L"horizontal",32644,false},{L"vertical",32645,false},
            {L"move",32646,false},{L"unavailable",32648,false},{L"hand",32649,false},{L"working",32650,false},{L"help",32651,false},
            {L"pen",32631,true},{L"pin",32671,true},{L"person",32672,true}};
        int size=std::clamp(MulDiv(32*g_app.settings.cursor_scale,int(GetDpiForSystem()),9600),16,256);
        int nearest=32;for(int option:{32,48,64,96,128}) if(abs(option-size)<abs(nearest-size)) nearest=option;
        std::array<HCURSOR,17> cursors{};scope_exit clean([&]{for(auto cursor:cursors) if(cursor) DestroyCursor(cursor);});
        for(size_t n=0;n<std::size(roles);++n) {
            bool animated=!wcscmp(roles[n].name,L"busy")||!wcscmp(roles[n].name,L"working");
            auto name=std::wstring(roles[n].name)+(animated?L"-"+std::to_wstring(nearest)+L".ani":L".cur");
            cursors[n]=static_cast<HCURSOR>(LoadImageW(nullptr,(directory/name).c_str(),IMAGE_CURSOR,size,size,LR_LOADFROMFILE));
            if(!cursors[n]&&!roles[n].optional) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        }
        if(!owned) { if(!write_utf8_atomic(receipt().c_str(),L"{\"schema\":1,\"native_pointer\":true}\n")) throw winrt::hresult_error(E_ACCESSDENIED);owned=true; }
        for(size_t n=0;n<std::size(roles);++n) if(cursors[n]) { if(SetSystemCursor(cursors[n],roles[n].id)) cursors[n]=nullptr;else if(!roles[n].optional) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError())); }
    }
};
bool launch(bool recovery) {
    DWORD pid=GetCurrentProcessId();stop.reset(CreateEventW(nullptr,TRUE,FALSE,event_name(L"Stop",pid).c_str()));changed.reset(CreateEventW(nullptr,FALSE,FALSE,event_name(L"Changed",pid).c_str()));if(!stop||!changed) return false;ResetEvent(stop.get());
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) return false;ULONGLONG timestamp=(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    wchar_t executable[LB_PATH*2],data[LB_PATH*2];if(!quote_arg(executable,ARRAYSIZE(executable),g_app.exe)||!quote_arg(data,ARRAYSIZE(data),g_app.data)) return false;
    auto command=std::wstring(executable)+L" --pointer-helper --parent-pid "+std::to_wstring(pid)+L" --parent-created "+std::to_wstring(timestamp)+L" --data-dir "+data+(recovery?L" --restore-only":L"");
    STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION child{};if(!CreateProcessW(g_app.exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&child)) return false;
    CloseHandle(child.hThread);process.reset(child.hProcess);closing=recovery;SetTimer(g_app.window,20,120,nullptr);return true;
}
}
int helper_main(int argc,wchar_t **argv) {
    DWORD pid=0;ULONGLONG timestamp=0;bool recovery=false;
    for(int n=1;n<argc;++n) { if(!wcscmp(argv[n],L"--parent-pid")&&n+1<argc) pid=wcstoul(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--parent-created")&&n+1<argc) timestamp=_wcstoui64(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--restore-only")) recovery=true; }
    try {
        unique_handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));FILETIME created{},exit{},kernel{},user{};
        if(!parent||!GetProcessTimes(parent.get(),&created,&exit,&kernel,&user)||((ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=timestamp||WaitForSingleObject(parent.get(),0)!=WAIT_TIMEOUT) return ERROR_INVALID_PARAMETER;
        unique_handle lease(CreateMutexW(nullptr,FALSE,L"Local\\Qingkuai.PointerStyle"));bool busy=GetLastError()==ERROR_ALREADY_EXISTS;if(!lease||busy) return ERROR_BUSY;
        SchemeLease scheme;scheme.recover();if(recovery) return 0;
        unique_handle stop_event(OpenEventW(SYNCHRONIZE,FALSE,event_name(L"Stop",pid).c_str())),change_event(OpenEventW(SYNCHRONIZE,FALSE,event_name(L"Changed",pid).c_str()));if(!stop_event||!change_event) return ERROR_INVALID_HANDLE;
        scheme.apply();HANDLE events[]={parent.get(),stop_event.get(),change_event.get()};
        for(;;) {
            DWORD event=WaitForMultipleObjects(3,events,FALSE,INFINITE);if(event==WAIT_OBJECT_0||event==WAIT_OBJECT_0+1) break;
            if(event!=WAIT_OBJECT_0+2) return ERROR_INVALID_HANDLE;load_config();if(g_app.settings.cursor_theme==0) break;scheme.apply();
        }
        return scheme.restore()?0:ERROR_WRITE_FAULT;
    }catch(winrt::hresult_error const& error) { log_line(L"pointer helper failed 0x%08lX",DWORD(error.code()));return ERROR_INVALID_DATA; }
     catch(...) { log_line(L"pointer helper failed; recovery state retained");return ERROR_INVALID_DATA; }
}
bool configure() {
    bool wanted=g_app.settings.cursor_theme!=0;
    if(process&&WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT) { if(!wanted) return shutdown();if(closing) return true;SetEvent(changed.get());return true; }
    process.reset();closing=false;
    if(wanted) return launch(false);if(GetFileAttributesW(receipt().c_str())!=INVALID_FILE_ATTRIBUTES) { ++recovery_attempts;return launch(true); }return true;
}
bool has_session() { return bool(process); }
bool shutdown() {
    closing=true;if(stop) SetEvent(stop.get());if(process&&WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT) { SetTimer(g_app.window,20,120,nullptr);return false; }
    process.reset();stop.reset();changed.reset();KillTimer(g_app.window,20);return true;
}
namespace {
void complete_exit(DWORD code) {
    bool retiring=std::exchange(closing,false);
    if(retiring&&(g_app.settings.cursor_theme||g_app.close_requested)) {
        // Restoration from the old session cannot cancel a newer selection.
        // A fresh helper recovers its own durable receipt before applying it.
        if(!g_app.close_requested&&g_app.settings.cursor_theme&&!launch(false)) { g_app.settings.cursor_theme=0;save_config();set_status(L"指针美化未能重新启动，已保留系统指针",L"Pointer styling could not restart. System cursors were retained."); }
        return;
    }
    if(code) {
        g_app.settings.cursor_theme=0;save_config();set_status(code==ERROR_BUSY?L"其他程序正在管理指针，请先关闭其指针美化。":L"指针模块已退出，正在恢复系统指针",code==ERROR_BUSY?L"Another app owns pointer styling; no takeover was made.":L"Pointer module exited; restoring system cursors.");
        if(code!=ERROR_BUSY&&recovery_attempts<1&&GetFileAttributesW(receipt().c_str())!=INVALID_FILE_ATTRIBUTES) { ++recovery_attempts;launch(true); }
    }else if(!g_app.close_requested&&g_app.settings.cursor_theme&&!launch(false)) { g_app.settings.cursor_theme=0;save_config();set_status(L"指针美化未能启动",L"Pointer styling could not start."); }
}
}
void tick() {
    if(!process) { KillTimer(g_app.window,20);return; }if(WaitForSingleObject(process.get(),0)!=WAIT_OBJECT_0) return;
    DWORD code=0;GetExitCodeProcess(process.get(),&code);process.reset();stop.reset();changed.reset();KillTimer(g_app.window,20);
    complete_exit(code);
}
bool transition_tests() {
    if(process) return false;auto theme=g_app.settings.cursor_theme;auto requested=g_app.close_requested;bool prior=closing;
    scope_exit restore([&]{g_app.settings.cursor_theme=theme;g_app.close_requested=requested;closing=prior;});
    g_app.settings.cursor_theme=2;g_app.close_requested=TRUE;closing=true;complete_exit(ERROR_BUSY);
    return g_app.settings.cursor_theme==2&&!closing;
}
}
