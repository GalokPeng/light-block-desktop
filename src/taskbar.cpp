#include "taskbar.hpp"
#include "taskbar_recovery.hpp"
#include <sddl.h>
#include <fstream>
#include <filesystem>

namespace lb::taskbar {
namespace {
constexpr CLSID layout_id{0x8a34b1e2,0xc95d,0x49e6,{0x91,0xe7,0x0f,0x38,0x21,0xbe,0xb5,0x6d}};
constexpr wchar_t channel_base[]=L"Local\\LightBlockNative.TaskbarLayout.v1.";
struct Client {
    unique_handle process,mapping;
    Control *control{};
    bool stopping{};
    bool recovering{};
    bool recovery_failed{};
    ULONGLONG stop_started{};
    ~Client() { if(control) UnmapViewOfFile(control); }
} client;
std::wstring control_name(DWORD pid) { return L"Local\\LightBlockNative.TaskbarControl."+std::to_wstring(pid); }
DWORD window_pid(HWND window) { DWORD pid=0;GetWindowThreadProcessId(window,&pid);return pid; }
bool rect_equal(RECT const& a,RECT const& b) { return EqualRect(&a,&b)!=FALSE; }
struct ShellEpoch {
    HWND window{};DWORD pid{};ULONGLONG started{};
    void observe(HWND next,DWORD owner,ULONGLONG now) {if(next!=window||owner!=pid) {window=next;pid=owner;started=now;}}
    void healthy(ULONGLONG now) {started=now;}
    bool expired(ULONGLONG now,ULONGLONG limit) const {return window&&now-started>limit;}
};
bool status_read(LayoutStatus *source,LayoutStatus &result) {
    for(int n=0;n<3;++n) { LONG before=InterlockedCompareExchange(&source->sequence,0,0);if(before&1) continue;MemoryBarrier();memcpy(&result,source,sizeof result);MemoryBarrier();if(before==InterlockedCompareExchange(&source->sequence,0,0) && result.sequence==before && result.version==4) return true; }return false;
}
class LayoutSession {
    unique_handle events_[5],mapping_;
    LayoutStatus *status_{};
    HMODULE module_{},xaml_{};
    DWORD pid_{};
    HWND bar_{};
    std::wstring prefix_;
    std::wstring receipt() const { return (std::filesystem::path(g_app.data)/L"native-taskbar-session.ini").wstring(); }
    using Attach=HRESULT(WINAPI*)(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,CLSID,LPCWSTR);
    std::wstring name(wchar_t const* suffix) const { return prefix_+std::to_wstring(pid_)+L"."+suffix; }
public:
    ~LayoutSession() { close(); }
    void close() noexcept {
        if(events_[1] && events_[4] && WaitForSingleObject(events_[4].get(),0)==WAIT_OBJECT_0) { if(events_[0]) ResetEvent(events_[0].get());SetEvent(events_[1].get());if(events_[3]) WaitForSingleObject(events_[3].get(),2300); }
        if(status_) { UnmapViewOfFile(status_);status_=nullptr; }mapping_.reset();for(auto &event:events_) event.reset();
        if(module_) { FreeLibrary(module_);module_=nullptr; }if(xaml_) { FreeLibrary(xaml_);xaml_=nullptr; }pid_=0;bar_=nullptr;
    }
    HRESULT initialize(HWND bar) {
        close();pid_=window_pid(bar);bar_=bar;if(!pid_) return E_PENDING;
        wchar_t old_prefix[180];GetPrivateProfileStringW(L"layout",L"prefix",L"",old_prefix,180,receipt().c_str());
        if(GetPrivateProfileIntW(L"layout",L"explorer",0,receipt().c_str())==pid_ && std::wstring_view(old_prefix).starts_with(channel_base)) {
            std::wstring prefix=old_prefix,suffix=prefix.substr(wcslen(channel_base));
            bool valid=suffix.size()>=2 && suffix.size()<=16 && suffix.back()==L'.';for(size_t n=0;valid&&n+1<suffix.size();++n) valid=iswdigit(suffix[n])!=0;
            if(valid) { auto base=prefix+std::to_wstring(pid_)+L".";unique_handle stop(OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L"Stop").c_str())),restored(OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(base+L"Restored").c_str()));
                if(stop && restored) { ResetEvent(restored.get());SetEvent(stop.get());if(WaitForSingleObject(restored.get(),2300)!=WAIT_OBJECT_0) return E_PENDING; }
            }
        }
        wchar_t runtime[LB_PATH],current[LB_PATH];path_join(runtime,LB_PATH,g_app.directory,L"runtime");path_join(current,LB_PATH,runtime,L"taskbar-current.txt");
        auto text=std::unique_ptr<wchar_t,decltype(&std::free)>(read_utf8_file(current,1024),&std::free);if(!text) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        std::wstring filename=text.get();while(!filename.empty() && iswspace(filename.back())) filename.pop_back();
        if(filename.find(L"LightBlockNativeTaskbar-")!=0 || filename.find_first_of(L"/\\:")!=std::wstring::npos || !filename.ends_with(L".dll")) return E_INVALIDARG;
        auto original=std::filesystem::path(runtime)/filename;module_=LoadLibraryExW(original.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module_) return HRESULT_FROM_WIN32(GetLastError());
        using Build=unsigned long long(WINAPI*)();auto build=reinterpret_cast<Build>(GetProcAddress(module_,"LightBlockNativeLayoutBuild"));if(!build) return E_NOINTERFACE;
        prefix_=channel_base+std::to_wstring(build())+L".";
        WritePrivateProfileStringW(L"layout",L"prefix",prefix_.c_str(),receipt().c_str());WritePrivateProfileStringW(L"layout",L"explorer",std::to_wstring(pid_).c_str(),receipt().c_str());
        PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;OW)(A;;GA;;;SY)S:(ML;;NW;;;ME)",SDDL_REVISION_1,&descriptor,nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        scope_exit free_descriptor([&]{LocalFree(descriptor);});SECURITY_ATTRIBUTES security{sizeof security,descriptor,FALSE};
        wchar_t const* suffixes[]={L"Start",L"Stop",L"Ready",L"Restored",L"Resident"};
        for(int n=0;n<5;++n) { events_[n].reset(CreateEventW(&security,n>=2,FALSE,name(suffixes[n]).c_str()));if(!events_[n]) return HRESULT_FROM_WIN32(GetLastError()); }
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(LayoutStatus),name(L"Status").c_str()));if(!mapping_) return HRESULT_FROM_WIN32(GetLastError());
        status_=static_cast<LayoutStatus*>(MapViewOfFile(mapping_.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(LayoutStatus)));if(!status_) return HRESULT_FROM_WIN32(GetLastError());
        InterlockedExchange(&status_->appearance_reserved,LONG(GetCurrentProcessId()));
        /* Only this product's build namespace is touched. Never stop the original component. */
        if(WaitForSingleObject(events_[4].get(),0)!=WAIT_OBJECT_0) {
            std::filesystem::path cache=std::filesystem::path(g_app.data)/L"native-runtime";std::error_code error;std::filesystem::create_directories(cache,error);if(error) return E_ACCESSDENIED;
            auto staged=cache/filename;if(!std::filesystem::exists(staged,error)) { if(error) return E_ACCESSDENIED;std::filesystem::copy_file(original,staged,std::filesystem::copy_options::none,error);if(error) return E_ACCESSDENIED; }
            xaml_=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!xaml_) return E_NOINTERFACE;
            auto attach=reinterpret_cast<Attach>(GetProcAddress(xaml_,"InitializeXamlDiagnosticsEx"));if(!attach) return E_NOINTERFACE;
            HRESULT result=HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            for(int n=1;n<=128;++n) { auto endpoint=L"VisualDiagConnection"+std::to_wstring(n);result=attach(endpoint.c_str(),pid_,L"",staged.c_str(),layout_id,nullptr);if(result!=HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break; }
            if(FAILED(result)) return result;
        }
        ResetEvent(events_[1].get());ResetEvent(events_[2].get());ResetEvent(events_[3].get());SetEvent(events_[0].get());return S_OK;
    }
    bool same(HWND bar) const noexcept { return status_ && bar==bar_ && window_pid(bar)==pid_; }
    void appearance(Control const& control) {
        if(!status_) return;LONG rgb=LONG((GetRValue(control.color)<<16)|(GetGValue(control.color)<<8)|GetBValue(control.color));
        if(status_->appearance_color==rgb && status_->appearance_opacity==control.opacity && status_->appearance_radius==control.radius && status_->appearance_flags==1) return;
        status_->appearance_color=rgb;status_->appearance_opacity=control.opacity;status_->appearance_radius=control.radius;status_->appearance_flags=1;MemoryBarrier();InterlockedIncrement(&status_->appearance_revision);SetEvent(events_[0].get());
    }
    bool status(LayoutStatus &out) { return status_ && status_read(status_,out) && events_[2] && WaitForSingleObject(events_[2].get(),0)==WAIT_OBJECT_0; }
};
struct WorkArea {
    RECT before{},after{};bool owned{};
    static BOOL CALLBACK resize(HWND window,LPARAM context) {
        auto self=reinterpret_cast<WorkArea*>(context);if(!IsWindowVisible(window)||!IsZoomed(window)) return TRUE;
        RECT frame{};if(SUCCEEDED(DwmGetWindowAttribute(window,9,&frame,sizeof frame)) && abs(frame.bottom-self->before.bottom)<=2 && frame.left>=self->before.left-2 && frame.right<=self->before.right+2) { RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top+self->after.bottom-frame.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING|SWP_ASYNCWINDOWPOS); }return TRUE;
    }
    void expand(HWND window,RECT const& bar,RecoveryLease& recovery) {
        if(owned) return;RECT current{};if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&current,0)||current.bottom!=bar.top) return;
        before=current;after=current;after.bottom=bar.bottom;
        if(!recovery.work(window,before,after)) return;
        if(SystemParametersInfoW(SPI_SETWORKAREA,0,&after,0)) { owned=true;EnumWindows(resize,reinterpret_cast<LPARAM>(this)); }
    }
    void restore() noexcept {
        if(!owned) return;RECT current{};if(SystemParametersInfoW(SPI_GETWORKAREA,0,&current,0) && rect_equal(current,after)) { SystemParametersInfoW(SPI_SETWORKAREA,0,&before,0);std::swap(before,after);EnumWindows(resize,reinterpret_cast<LPARAM>(this)); }owned=false;
    }
    ~WorkArea() { restore(); }
};
struct Visibility {
    HWND bar{};DWORD pid{};bool hidden{},topmost_changed{};unique_gdi<HRGN> saved;WorkArea work;
    RecoveryLease recovery;
    bool same() const { return IsWindow(bar)&&window_pid(bar)==pid && FindWindowW(L"Shell_TrayWnd",nullptr)==bar; }
    bool ours() const { unique_gdi<HRGN> current(CreateRectRgn(0,0,0,0)),empty(CreateRectRgn(0,0,0,0));return GetWindowRgn(bar,current.get())!=ERROR && EqualRgn(current.get(),empty.get()); }
    void restore_region() {
        if(hidden && same() && ours()) { if(saved) { auto copy=CreateRectRgn(0,0,0,0);CombineRgn(copy,saved.get(),nullptr,RGN_COPY);if(!SetWindowRgn(bar,copy,TRUE)) DeleteObject(copy); }else SetWindowRgn(bar,nullptr,TRUE); }hidden=false;saved.reset();
    }
    void restore() {
        restore_region();if(topmost_changed && same()) SetWindowPos(bar,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);topmost_changed=false;work.restore();if(!recovery.restore()) log_line(L"Taskbar visibility recovery remains pending");bar=nullptr;pid=0;
    }
    void update(HWND current,RECT const& bounds,bool hide,bool reveal) {
        if(current!=bar || window_pid(current)!=pid) { restore();bar=current;pid=window_pid(current); }
        APPBARDATA data{sizeof data};if(SHAppBarMessage(ABM_GETSTATE,&data)&ABS_AUTOHIDE) { restore();return; }work.expand(bar,bounds,recovery);
        if(hide && !hidden) {
            unique_gdi<HRGN> previous(CreateRectRgn(0,0,0,0));int kind=GetWindowRgn(bar,previous.get());RECT r{};GetRgnBox(previous.get(),&r);
            if(kind!=ERROR && (kind!=SIMPLEREGION || r.left || r.top || r.right!=bounds.right-bounds.left || r.bottom!=bounds.bottom-bounds.top)) return;
            if(!recovery.region(bar,kind,bounds)) return;
            if(kind!=ERROR) saved=std::move(previous);auto empty=CreateRectRgn(0,0,0,0);if(SetWindowRgn(bar,empty,TRUE)) hidden=true;else DeleteObject(empty);
        } else if(!hide) restore_region();
        if(reveal && !(GetWindowLongPtrW(bar,GWL_EXSTYLE)&WS_EX_TOPMOST)) { if(recovery.topmost(bar)&&SetWindowPos(bar,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)) topmost_changed=true; }
        else if(!reveal && topmost_changed) { SetWindowPos(bar,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);topmost_changed=false; }
    }
    ~Visibility() { restore(); }
};
struct Coverage { RECT strip;HWND bar;bool covered{}; };
BOOL CALLBACK covered_window(HWND window,LPARAM value) {
    auto &coverage=*reinterpret_cast<Coverage*>(value);if(window==coverage.bar || !IsWindowVisible(window)||IsIconic(window)) return TRUE;
    LONG_PTR ex=GetWindowLongPtrW(window,GWL_EXSTYLE);if(ex&WS_EX_TRANSPARENT) return TRUE;
    wchar_t type[100];GetClassNameW(window,type,100);if(!wcscmp(type,L"Progman")||!wcscmp(type,L"WorkerW")||!wcsncmp(type,L"Shell_",6)) return TRUE;
    HWND owner=GetWindow(window,GW_OWNER);if(owner==GetShellWindow()) return TRUE;
    DWORD cloaked=0;DwmGetWindowAttribute(window,14,&cloaked,sizeof cloaked);if(cloaked) return TRUE;
    RECT rect{};if(FAILED(DwmGetWindowAttribute(window,9,&rect,sizeof rect))) GetWindowRect(window,&rect);RECT intersection{};
    if(IntersectRect(&intersection,&rect,&coverage.strip) && intersection.right-intersection.left>3 && intersection.bottom-intersection.top>3) { coverage.covered=true;return FALSE; }return TRUE;
}
bool start_recovery() {
    if(client.process||!recovery_pending()) return false;
    wchar_t executable[LB_PATH*2+4],profile[LB_PATH*2+4],command[LB_PATH*4+220];
    if(!quote_arg(executable,ARRAYSIZE(executable),g_app.exe)||!quote_arg(profile,ARRAYSIZE(profile),g_app.data)) return false;
    if(FAILED(StringCchPrintfW(command,ARRAYSIZE(command),L"%s --taskbar-helper --recover-only --parent-pid %lu --data-dir %s",executable,GetCurrentProcessId(),profile))) return false;
    STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION process{};
    if(!CreateProcessW(g_app.exe,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&process)) return false;
    CloseHandle(process.hThread);client.process.reset(process.hProcess);client.stopping=true;client.recovering=true;client.recovery_failed=false;client.stop_started=GetTickCount64();if(g_app.window) SetTimer(g_app.window,16,250,nullptr);return true;
}
}
void configure() {
    if(!g_app.settings.taskbar_enabled) { shutdown();return; }
    // A retiring helper owns restoration until it exits. Re-enabling queues a
    // fresh session; publishing enabled=1 here could revive half-restored state.
    if(client.process&&client.stopping) return;
    if(!client.control) {
        client.mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Control),control_name(GetCurrentProcessId()).c_str()));if(!client.mapping) return;
        client.control=static_cast<Control*>(MapViewOfFile(client.mapping.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Control)));if(!client.control) return;
    }
    FILETIME created{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);
    InterlockedIncrement(&client.control->sequence);client.control->enabled=1;client.control->color=LONG(g_app.settings.taskbar_color);client.control->opacity=g_app.settings.taskbar_opacity;client.control->radius=g_app.settings.taskbar_radius;client.control->parent_created=(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime;MemoryBarrier();InterlockedIncrement(&client.control->sequence);client.stopping=false;
    if(client.process) return;
    wchar_t executable[LB_PATH*2+4],profile[LB_PATH*2+4],command[LB_PATH*4+200];quote_arg(executable,ARRAYSIZE(executable),g_app.exe);quote_arg(profile,ARRAYSIZE(profile),g_app.data);
    StringCchPrintfW(command,ARRAYSIZE(command),L"%s --taskbar-helper --parent-pid %lu --data-dir %s",executable,GetCurrentProcessId(),profile);
    STARTUPINFOW startup{sizeof startup};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(CreateProcessW(g_app.exe,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&process)) { client.process.reset(process.hProcess);CloseHandle(process.hThread);SetTimer(g_app.window,16,250,nullptr); }
}
bool has_session() { return bool(client.process); }
bool transition_tests() {
    if(client.process||client.control||client.mapping) return false;
    auto wanted=g_app.settings.taskbar_enabled;bool prior=client.stopping;Control control{};
    scope_exit restore([&]{client.process.reset();client.control=nullptr;client.stopping=prior;g_app.settings.taskbar_enabled=wanted;});
    client.process.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!client.process) return false;client.control=&control;client.stopping=true;g_app.settings.taskbar_enabled=1;
    configure();bool retired=client.stopping&&control.enabled==0&&control.sequence==0&&g_app.settings.taskbar_enabled==1;
    ShellEpoch epoch;epoch.observe(HWND(uintptr_t(1)),10,100);if(epoch.expired(10100,15000)) return false;
    epoch.observe(HWND(uintptr_t(2)),20,100000);if(epoch.expired(100120,15000)||!epoch.expired(116000,15000)) return false;
    epoch.observe(HWND(uintptr_t(3)),20,120000);if(epoch.expired(120120,15000)) return false;
    epoch.observe(nullptr,0,130000);if(epoch.expired(200000,15000)) return false;
    epoch.observe(HWND(uintptr_t(3)),20,200001);return retired&&!epoch.expired(200100,15000);
}
bool shutdown() { if(client.control) InterlockedExchange(&client.control->enabled,0);if(!client.stopping) client.stop_started=GetTickCount64();client.stopping=true;if(client.process) {if(g_app.window) SetTimer(g_app.window,16,250,nullptr);return false;}if(!client.recovery_failed&&recovery_pending()) {if(start_recovery()) return false;client.recovery_failed=true;log_line(L"Taskbar recovery launch failed; receipt retained for next startup");}return true; }
void tick() {
    if(client.process&&client.stopping&&client.stop_started&&WaitForSingleObject(client.process.get(),0)==WAIT_TIMEOUT&&GetTickCount64()-client.stop_started>8000) {log_line(L"Taskbar helper shutdown timed out; using durable recovery receipt");TerminateProcess(client.process.get(),ERROR_TIMEOUT);}
    if(!client.process || WaitForSingleObject(client.process.get(),0)==WAIT_OBJECT_0) {
        DWORD code=0;if(client.process) GetExitCodeProcess(client.process.get(),&code);client.process.reset();KillTimer(g_app.window,16);
        bool retiring=std::exchange(client.stopping,false);
        bool recovering=std::exchange(client.recovering,false);
        if(recovering&&(code||recovery_pending())) {client.recovery_failed=true;g_app.settings.taskbar_enabled=0;save_config();set_status(L"任务栏恢复暂未完成，恢复记录已保留；请稍后重新打开轻块桌面",L"Taskbar recovery is pending. Its recovery record is preserved; reopen Light Block Desktop to retry.");return;}
        if(code&&!recovering&&recovery_pending()) {if(start_recovery()) {set_status(L"任务栏组件已退出，正在恢复系统任务栏…",L"The taskbar component exited. Restoring the Windows taskbar…");return;}}
        if(code&&!retiring) { g_app.settings.taskbar_enabled=0;save_config();set_status(code==ERROR_BUSY?L"另一个轻块版本正在管理任务栏，请先关闭其任务栏美化":L"任务栏组件未能完成初始化，已停止美化",code==ERROR_BUSY?L"Another Light Block version owns the taskbar. Disable its styling first.":L"Taskbar initialization failed. Styling has been stopped."); }
        else if(!g_app.close_requested&&g_app.settings.taskbar_enabled) configure();
    }
}
int helper_main(int argc,wchar_t **argv) {
    try {
    DWORD parent_pid=0;bool recover_only=false;for(int n=1;n<argc;++n) {if(!wcscmp(argv[n],L"--parent-pid") && n+1<argc) parent_pid=wcstoul(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--recover-only")) recover_only=true;}
    unique_handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));if(!parent) return ERROR_INVALID_PARAMETER;
    unique_handle lease(CreateMutexW(nullptr,FALSE,L"Local\\Still.TaskbarStyle"));bool busy=GetLastError()==ERROR_ALREADY_EXISTS;if(!lease||busy) return ERROR_BUSY;
    if(!recover_visibility()) return ERROR_INVALID_DATA;if(recover_only) return 0;
    unique_handle mapping(OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,control_name(parent_pid).c_str()));if(!mapping) return ERROR_INVALID_PARAMETER;
    auto control=static_cast<Control*>(MapViewOfFile(mapping.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Control)));if(!control) return ERROR_INVALID_PARAMETER;scope_exit unmap([&]{UnmapViewOfFile(control);});
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(parent.get(),&created,&exit,&kernel,&user) || control->parent_created!=((ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime)) return ERROR_INVALID_PARAMETER;
    com_apartment apartment(COINIT_APARTMENTTHREADED);if(FAILED(apartment.result())) return ERROR_NOT_SUPPORTED;
    LayoutSession session;Visibility visibility;ShellEpoch epoch;
    while(WaitForSingleObject(parent.get(),120)==WAIT_TIMEOUT && InterlockedCompareExchange(&control->enabled,0,0)) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message);DispatchMessageW(&message); }
        LONG before=InterlockedCompareExchange(&control->sequence,0,0);if(before&1) continue;Control wanted=*control;MemoryBarrier();if(before!=InterlockedCompareExchange(&control->sequence,0,0)) continue;
        HWND bar=FindWindowW(L"Shell_TrayWnd",nullptr);epoch.observe(bar,window_pid(bar),GetTickCount64());if(!bar) continue;
        if(!session.same(bar)) { visibility.restore();HRESULT result=session.initialize(bar);if(FAILED(result)) { if(epoch.expired(GetTickCount64(),12000)) { log_line(L"taskbar attach failed 0x%08lX",DWORD(result));return ERROR_NOT_SUPPORTED; }continue; } }
        session.appearance(wanted);LayoutStatus status{};if(!session.status(status)||status.state!=1) { if(status.state==3||status.state==4) return ERROR_NOT_SUPPORTED;if(epoch.expired(GetTickCount64(),15000)) return ERROR_TIMEOUT;continue; }epoch.healthy(GetTickCount64());
        RECT bounds{};GetWindowRect(bar,&bounds);if(!(status.host_width>0 && status.group_left>=0 && status.group_right>status.group_left && status.group_right<=status.host_width)) continue;
        double scale=double(bounds.right-bounds.left)/status.host_width;RECT strip{bounds.left+LONG(std::lround(status.group_left*scale)),bounds.top,bounds.left+LONG(std::lround(status.group_right*scale)),bounds.bottom};
        Coverage coverage{strip,bar};EnumWindows(covered_window,reinterpret_cast<LPARAM>(&coverage));POINT cursor{};GetCursorPos(&cursor);
        bool edge=cursor.y>=bounds.bottom-2 && cursor.x>=bounds.left && cursor.x<bounds.right;RECT hover=strip;InflateRect(&hover,5,6);bool reveal=coverage.covered && (edge||PtInRect(&hover,cursor));visibility.update(bar,bounds,coverage.covered&&!reveal,reveal);
    }
    return 0;
    } catch(std::exception const&) { log_line(L"taskbar helper exception; restoring through RAII");return ERROR_NOT_SUPPORTED; }
}
}
