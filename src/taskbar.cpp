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
std::wstring layout_receipt() {return (std::filesystem::path(g_app.data)/L"native-taskbar-session.ini").wstring();}
bool layout_pending() {std::error_code error;return std::filesystem::exists(layout_receipt(),error);}
bool repair_recorded_layout(HWND bar) {
    if(!layout_pending())return true;
    auto path=layout_receipt();wchar_t buffer[180]{};GetPrivateProfileStringW(L"layout",L"prefix",L"",buffer,180,path.c_str());
    std::wstring prefix=buffer;DWORD pid{};if(bar)GetWindowThreadProcessId(bar,&pid);
    DWORD recorded=GetPrivateProfileIntW(L"layout",L"explorer",0,path.c_str());
    if(recorded&&recorded==pid) {
        if(!prefix.starts_with(channel_base))return false;
        auto suffix=prefix.substr(wcslen(channel_base));bool valid=suffix.size()>=2&&suffix.size()<=16&&suffix.back()==L'.';
        for(size_t n=0;valid&&n+1<suffix.size();++n)valid=iswdigit(suffix[n])!=0;if(!valid)return false;
        auto base=prefix+std::to_wstring(pid)+L".";
        unique_handle resident(OpenEventW(SYNCHRONIZE,FALSE,(base+L"Resident").c_str()));
        if(resident&&WaitForSingleObject(resident.get(),0)==WAIT_OBJECT_0) {
            unique_handle start(OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L"Start").c_str())),stop(OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L"Stop").c_str())),restored(OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(base+L"Restored").c_str()));
            if(!stop||!restored)return false;if(start)ResetEvent(start.get());ResetEvent(restored.get());SetEvent(stop.get());
            if(WaitForSingleObject(restored.get(),2300)!=WAIT_OBJECT_0)return false;
        }
    }
    std::error_code error;std::filesystem::remove(path,error);return !error;
}
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
        if(pid_&&!repair_recorded_layout(bar_))log_line(L"Taskbar XAML restoration pending; receipt retained");
        if(status_) { UnmapViewOfFile(status_);status_=nullptr; }mapping_.reset();for(auto &event:events_) event.reset();
        if(module_) { FreeLibrary(module_);module_=nullptr; }if(xaml_) { FreeLibrary(xaml_);xaml_=nullptr; }pid_=0;bar_=nullptr;
    }
    HRESULT initialize(HWND bar) {
        close();pid_=window_pid(bar);bar_=bar;if(!pid_) return E_PENDING;
        if(!repair_recorded_layout(bar))return E_PENDING;
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
    RECT before{},after{},footprint{};bool owned{};ULONGLONG next_check{};
    static BOOL CALLBACK resize(HWND window,LPARAM context) {
        auto self=reinterpret_cast<WorkArea*>(context);if(!IsWindowVisible(window)||!IsZoomed(window)) return TRUE;
        RECT frame{};if(SUCCEEDED(DwmGetWindowAttribute(window,9,&frame,sizeof frame)) && abs(frame.bottom-self->before.bottom)<=2 && frame.left>=self->before.left-2 && frame.right<=self->before.right+2) { RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top+self->after.bottom-frame.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING|SWP_ASYNCWINDOWPOS); }return TRUE;
    }
    template<class Read,class Write,class Record,class Adjust>
    void reconcile(RECT const& bar,ULONGLONG now,Read read,Write write,Record record,Adjust adjust) {
        if(now<next_check) return;next_check=now+1000;
        RECT current{};if(!read(current)) return;
        if(owned) {
            // Explorer can restore its reserved strip without replacing HWND.
            // Reclaim only our exact previous area and unchanged display geometry;
            // another app bar or a changed monitor layout belongs to its new owner.
            if(rect_equal(bar,footprint)&&rect_equal(current,before)&&write(after)) adjust();
            return;
        }
        if(current.bottom!=bar.top||bar.bottom<=bar.top||current.left<bar.left||current.right>bar.right) return;
        before=current;after=current;after.bottom=bar.bottom;footprint=bar;
        if(!record(before,after)) return;
        if(write(after)) {owned=true;adjust();}
    }
    void expand(HWND window,RECT const& bar,RecoveryLease& recovery) {
        reconcile(bar,GetTickCount64(),[](RECT& area){return SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0)!=FALSE;},
            [](RECT area){return SystemParametersInfoW(SPI_SETWORKAREA,0,&area,0)!=FALSE;},
            [&](RECT old_area,RECT new_area){return recovery.work(window,old_area,new_area);},
            [&]{EnumWindows(resize,reinterpret_cast<LPARAM>(this));});
    }
    void restore() noexcept {
        if(!owned) return;RECT current{};if(SystemParametersInfoW(SPI_GETWORKAREA,0,&current,0) && rect_equal(current,after)&&SystemParametersInfoW(SPI_SETWORKAREA,0,&before,0)) {std::swap(before,after);EnumWindows(resize,reinterpret_cast<LPARAM>(this)); }owned=false;next_check=0;
    }
    ~WorkArea() { restore(); }
};
struct HiddenRegion {
    bool hidden{};
    unique_gdi<HRGN> saved,observed{CreateRectRgn(0,0,0,0)};
    bool empty(HWND bar) const {return observed&&GetWindowRgn(bar,observed.get())==NULLREGION;}
    bool restore(HWND bar) {
        if(hidden&&empty(bar)) {
            // We only lease an unrestricted or full-window rectangle. Clearing
            // it restores that meaning at the CURRENT size; replaying a saved
            // pixel rectangle clips the right side after a DPI/display resize.
            if(!SetWindowRgn(bar,nullptr,TRUE)) return false;
        }
        hidden=false;saved.reset();return true;
    }
    template<class Record> bool apply(HWND bar,RECT const& bounds,bool hide,Record record) {
        if(!hide) return restore(bar);
        if(!observed||!IsWindow(bar)) return false;
        int kind=GetWindowRgn(bar,observed.get());if(kind==NULLREGION) return true;
        RECT r{};GetRgnBox(observed.get(),&r);
        if(kind!=ERROR&&(kind!=SIMPLEREGION||r.left||r.top||r.right!=bounds.right-bounds.left||r.bottom!=bounds.bottom-bounds.top)) return false;
        // Check the actual region every tick, not just our cached hidden flag.
        // Keep the original restoration receipt when Explorer overwrites it.
        if(!hidden) {
            if(!record(kind,bounds)) return false;
            saved.reset();if(kind!=ERROR) {saved.reset(CreateRectRgn(0,0,0,0));if(!saved) return false;CombineRgn(saved.get(),observed.get(),nullptr,RGN_COPY);}
        }
        auto blank=CreateRectRgn(0,0,0,0);if(!blank) return false;
        if(!SetWindowRgn(bar,blank,TRUE)) {DeleteObject(blank);return false;}
        hidden=true;return true;
    }
};
bool repair_window_extent(HWND window) {
    RECT bounds{},clip{};unique_gdi<HRGN> region(CreateRectRgn(0,0,0,0));
    if(!window||!region||!GetWindowRect(window,&bounds))return false;
    if(GetWindowRgn(window,region.get())!=SIMPLEREGION)return true;
    GetRgnBox(region.get(),&clip);
    // Recover the legacy full-size snapshot only. Preserve offset/rounded
    // third-party regions and do not expose an empty native auto-hide region.
    if(clip.left||clip.top||clip.right<=0||clip.bottom<=0)return true;
    if(clip.right==bounds.right-bounds.left&&clip.bottom==bounds.bottom-bounds.top)return true;
    if(!SetWindowRgn(window,nullptr,TRUE))return false;
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN);return true;
}
bool reveal_at_edge(bool covered,bool edge,bool hover,bool previously_revealed) {
    return covered&&(edge||(previously_revealed&&hover));
}
struct Visibility {
    HWND bar{};DWORD pid{};bool revealed{},topmost_changed{};HiddenRegion region;WorkArea work;
    RecoveryLease recovery;
    bool same() const { return IsWindow(bar)&&window_pid(bar)==pid && FindWindowW(L"Shell_TrayWnd",nullptr)==bar; }
    void restore_region() {
        if(same()) region.restore(bar);else {region.hidden=false;region.saved.reset();}revealed=false;
    }
    void restore() {
        restore_region();if(topmost_changed && same()) SetWindowPos(bar,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);topmost_changed=false;work.restore();if(!recovery.restore()) log_line(L"Taskbar visibility recovery remains pending");else {region.hidden=false;region.saved.reset();}bar=nullptr;pid=0;
    }
    void update(HWND current,RECT const& bounds,bool hide,bool reveal) {
        if(current!=bar || window_pid(current)!=pid) { restore();bar=current;pid=window_pid(current); }
        APPBARDATA data{sizeof data};if(SHAppBarMessage(ABM_GETSTATE,&data)&ABS_AUTOHIDE) { restore();return; }work.expand(bar,bounds,recovery);
        bool applied=region.apply(bar,bounds,hide,[&](int kind,RECT area){return recovery.region(bar,kind,area);});
        revealed=reveal&&applied;
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
    if(client.process||(!recovery_pending()&&!layout_pending())) return false;
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
    if(!client.process)client.recovery_failed=false;
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
bool layout_recovery_tests() {
    if(layout_pending())return false;
    HWND window=CreateWindowExW(0,L"STATIC",L"layout-restore-fixture",WS_POPUP,0,0,400,48,nullptr,nullptr,g_app.instance,nullptr);if(!window)return false;
    auto path=layout_receipt();scope_exit cleanup([&]{DestroyWindow(window);std::error_code error;std::filesystem::remove(path,error);});
    auto prefix=std::wstring(channel_base)+std::to_wstring(GetTickCount64())+L".",base=prefix+std::to_wstring(GetCurrentProcessId())+L".";
    unique_handle start(CreateEventW(nullptr,TRUE,TRUE,(base+L"Start").c_str())),stop(CreateEventW(nullptr,FALSE,FALSE,(base+L"Stop").c_str())),restored(CreateEventW(nullptr,TRUE,TRUE,(base+L"Restored").c_str())),resident(CreateEventW(nullptr,TRUE,TRUE,(base+L"Resident").c_str()));
    if(!start||!stop||!restored||!resident)return false;
    auto save=[&]{return WritePrivateProfileStringW(L"layout",L"prefix",prefix.c_str(),path.c_str())&&WritePrivateProfileStringW(L"layout",L"explorer",std::to_wstring(GetCurrentProcessId()).c_str(),path.c_str());};
    if(!save())return false;
    std::thread worker([&]{if(WaitForSingleObject(stop.get(),4000)==WAIT_OBJECT_0){Sleep(45);SetEvent(restored.get());}});
    auto began=GetTickCount64();bool repaired=repair_recorded_layout(window);worker.join();
    if(!repaired||GetTickCount64()-began<40||layout_pending()||WaitForSingleObject(start.get(),0)!=WAIT_TIMEOUT)return false;
    // No acknowledgement must retain the durable receipt for the next toggle.
    if(!save()||repair_recorded_layout(window)||!layout_pending())return false;
    worker=std::thread([&]{if(WaitForSingleObject(stop.get(),4000)==WAIT_OBJECT_0){Sleep(20);SetEvent(restored.get());}});
    repaired=repair_recorded_layout(window);worker.join();return repaired&&!layout_pending();
}
bool visibility_tests() {
    // Exercise real window-region APIs on a private, never-shown test window.
    // The system taskbar and desktop work area are not changed by this fixture.
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"taskbar-region-fixture",WS_POPUP,0,0,400,48,nullptr,nullptr,g_app.instance,nullptr);
    if(!window) return false;scope_exit close([&]{DestroyWindow(window);});
    RECT bar{0,1032,400,1080};HiddenRegion region;int receipts=0;
    auto record=[&](int,RECT){++receipts;return true;};
    if(!region.apply(window,bar,true,record)||!region.empty(window)||receipts!=1) return false;
    for(int n=0;n<100;++n) {
        if(!SetWindowRgn(window,nullptr,FALSE)||!region.apply(window,bar,true,record)||!region.empty(window)) return false;
    }
    if(receipts!=1||!region.restore(window)||GetWindowRgn(window,region.observed.get())!=ERROR) return false;
    HRGN full=CreateRectRgn(0,0,400,48);if(!SetWindowRgn(window,full,FALSE)) {DeleteObject(full);return false;}
    if(!region.apply(window,bar,true,record)) return false;
    SetWindowPos(window,nullptr,0,0,800,72,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(!region.restore(window)||GetWindowRgn(window,region.observed.get())!=ERROR) return false;
    full=CreateRectRgn(0,0,400,48);if(!SetWindowRgn(window,full,FALSE)){DeleteObject(full);return false;}
    if(!repair_window_extent(window)||GetWindowRgn(window,region.observed.get())!=ERROR)return false;
    RECT actual{};bar.right=800;bar.top=1008;
    if(!region.apply(window,bar,true,record)) return false;
    HRGN foreign=CreateRectRgn(10,3,300,40);if(!SetWindowRgn(window,foreign,FALSE)) {DeleteObject(foreign);return false;}
    if(region.apply(window,bar,true,record)||!region.restore(window)||GetWindowRgn(window,region.observed.get())!=SIMPLEREGION) return false;
    GetRgnBox(region.observed.get(),&actual);if(actual.left!=10||actual.bottom!=40) return false;
    if(!repair_window_extent(window)||GetWindowRgn(window,region.observed.get())!=SIMPLEREGION)return false;
    // A screenshot finishing while the pointer is over the old taskbar strip
    // must not reveal it. Only the screen edge starts a reveal session.
    if(reveal_at_edge(true,false,true,false)||!reveal_at_edge(true,true,false,false)||
       !reveal_at_edge(true,false,true,true)||reveal_at_edge(true,false,false,true)||reveal_at_edge(false,true,true,true)) return false;
    WorkArea work;scope_exit disarm([&]{work.owned=false;});
    bar={0,1032,400,1080};
    RECT area{0,0,400,1032};int reads=0,writes=0,adjustments=0,records=0;bool fail=false;
    auto step=[&](ULONGLONG now,RECT bounds){work.reconcile(bounds,now,[&](RECT& value){++reads;value=area;return true;},
        [&](RECT value){if(fail) return false;++writes;area=value;return true;},
        [&](RECT,RECT){++records;return true;},[&]{++adjustments;});};
    step(1,bar);if(!work.owned||area.bottom!=1080||writes!=1||records!=1||adjustments!=1) return false;
    for(int n=2;n<1000;n+=100) step(n,bar);if(reads!=1||writes!=1) return false;
    for(int n=1;n<=100;++n) {area=work.before;step(1+n*1000,bar);if(area.bottom!=1080) return false;}
    if(records!=1||writes!=101||adjustments!=101) return false;
    area={0,0,400,1000};step(101001,bar);if(writes!=101||area.bottom!=1000) return false;
    area=work.before;RECT moved=bar;moved.right=500;step(102001,moved);if(writes!=101) return false;
    fail=true;step(103001,bar);if(writes!=101||area.bottom!=1032) return false;
    fail=false;step(104001,bar);return writes==102&&area.bottom==1080&&records==1;
}
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
bool shutdown_tests() {
    if(client.process||client.control||client.mapping||recovery_pending()) return false;
    auto old_window=g_app.window;auto old_closing=g_app.close_requested;auto old_enabled=g_app.settings.taskbar_enabled;
    auto old_stopping=client.stopping;auto old_started=client.stop_started;
    HWND fixture=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Shutdown fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,g_app.instance,nullptr);
    if(!fixture)return false;
    scope_exit restore([&]{client.process.reset();client.control=nullptr;client.stopping=old_stopping;client.stop_started=old_started;KillTimer(fixture,16);DestroyWindow(fixture);g_app.window=old_window;g_app.close_requested=old_closing;g_app.settings.taskbar_enabled=old_enabled;});
    Control control{};control.enabled=1;client.control=&control;
    client.process.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!client.process) return false;
    g_app.window=fixture;g_app.close_requested=TRUE;g_app.settings.taskbar_enabled=1;client.stopping=false;
    SetTimer(fixture,16,250,nullptr);
    if(shutdown()||control.enabled||!client.stopping)return false;
    auto began=client.stop_started;Sleep(20);if(shutdown()||began!=client.stop_started)return false;
    SetEvent(client.process.get());
    // No timer dispatch: the old implementation still returned false forever.
    return shutdown()&&!client.process&&g_app.settings.taskbar_enabled==1&&shutdown();
}
bool shutdown() {
    if(client.control) InterlockedExchange(&client.control->enabled,0);
    if(!client.stopping) client.stop_started=GetTickCount64();
    client.stopping=true;
    // Exit polls every 100 ms. Re-arming the 250 ms helper timer here would
    // postpone it forever. Reap directly; the existing timer also serves the
    // settings toggle when the application itself is not exiting.
    if(client.process) { tick();if(client.process) return false; }
    if(!client.recovery_failed&&(recovery_pending()||layout_pending())) {
        if(start_recovery()) return false;
        client.recovery_failed=true;log_line(L"Taskbar recovery launch failed; receipt retained for next startup");
    }
    return true;
}
void tick() {
    if(client.process&&client.stopping&&client.stop_started&&WaitForSingleObject(client.process.get(),0)==WAIT_TIMEOUT&&GetTickCount64()-client.stop_started>8000) {log_line(L"Taskbar helper shutdown timed out; using durable recovery receipt");TerminateProcess(client.process.get(),ERROR_TIMEOUT);}
    if(!client.process || WaitForSingleObject(client.process.get(),0)==WAIT_OBJECT_0) {
        DWORD code=0;if(client.process) GetExitCodeProcess(client.process.get(),&code);client.process.reset();KillTimer(g_app.window,16);
        bool retiring=std::exchange(client.stopping,false);
        bool recovering=std::exchange(client.recovering,false);
        if(recovering&&(code||recovery_pending()||layout_pending())) {client.recovery_failed=true;g_app.settings.taskbar_enabled=0;save_config();set_status(L"任务栏恢复暂未完成，恢复记录已保留；请稍后重新打开轻块桌面",L"Taskbar recovery is pending. Its recovery record is preserved; reopen Light Block Desktop to retry.");return;}
        if(!recovering&&!client.recovery_failed&&(recovery_pending()||layout_pending())) {if(start_recovery()) {set_status(L"正在检查并恢复系统任务栏…",L"Checking and restoring the Windows taskbar…");return;}}
        if(code&&!retiring) { g_app.settings.taskbar_enabled=0;save_config();set_status(code==ERROR_BUSY?L"另一个轻块版本正在管理任务栏，请先关闭其任务栏美化":L"任务栏组件未能完成初始化，已停止美化",code==ERROR_BUSY?L"Another Light Block version owns the taskbar. Disable its styling first.":L"Taskbar initialization failed. Styling has been stopped."); }
        else if(!g_app.close_requested&&g_app.settings.taskbar_enabled) configure();
    }
}
int helper_main(int argc,wchar_t **argv) {
    try {
    DWORD parent_pid=0;bool recover_only=false;for(int n=1;n<argc;++n) {if(!wcscmp(argv[n],L"--parent-pid") && n+1<argc) parent_pid=wcstoul(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--recover-only")) recover_only=true;}
    unique_handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));if(!parent) return ERROR_INVALID_PARAMETER;
    unique_handle lease(CreateMutexW(nullptr,FALSE,L"Local\\Still.TaskbarStyle"));bool busy=GetLastError()==ERROR_ALREADY_EXISTS;if(!lease||busy) return ERROR_BUSY;
    if(!repair_recorded_layout(FindWindowW(L"Shell_TrayWnd",nullptr))||!recover_visibility()) return ERROR_INVALID_DATA;
    auto repair_bar=FindWindowW(L"Shell_TrayWnd",nullptr);auto repair_pid=window_pid(repair_bar);
    if(repair_bar&&!repair_window_extent(repair_bar))log_line(L"Taskbar extent repair pending");
    scope_exit repair_after([&]{if(repair_bar&&IsWindow(repair_bar)&&window_pid(repair_bar)==repair_pid&&!repair_window_extent(repair_bar))log_line(L"Taskbar extent restoration pending");});
    if(recover_only) return 0;
    unique_handle mapping(OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,control_name(parent_pid).c_str()));if(!mapping) return ERROR_INVALID_PARAMETER;
    auto control=static_cast<Control*>(MapViewOfFile(mapping.get(),FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Control)));if(!control) return ERROR_INVALID_PARAMETER;scope_exit unmap([&]{UnmapViewOfFile(control);});
    FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(parent.get(),&created,&exit,&kernel,&user) || control->parent_created!=((ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime)) return ERROR_INVALID_PARAMETER;
    com_apartment apartment(COINIT_APARTMENTTHREADED);if(FAILED(apartment.result())) return ERROR_NOT_SUPPORTED;
    LayoutSession session;Visibility visibility;ShellEpoch epoch;LayoutStatus last_layout{};
    while(WaitForSingleObject(parent.get(),120)==WAIT_TIMEOUT && InterlockedCompareExchange(&control->enabled,0,0)) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message);DispatchMessageW(&message); }
        LONG before=InterlockedCompareExchange(&control->sequence,0,0);if(before&1) continue;Control wanted=*control;MemoryBarrier();if(before!=InterlockedCompareExchange(&control->sequence,0,0)) continue;
        HWND bar=FindWindowW(L"Shell_TrayWnd",nullptr);epoch.observe(bar,window_pid(bar),GetTickCount64());if(!bar) continue;
        if(!session.same(bar)) { visibility.restore();last_layout={};HRESULT result=session.initialize(bar);if(FAILED(result)) { if(epoch.expired(GetTickCount64(),12000)) { log_line(L"taskbar attach failed 0x%08lX",DWORD(result));return ERROR_NOT_SUPPORTED; }continue; } }
        session.appearance(wanted);LayoutStatus status{};
        if(session.status(status)&&status.state==1) {epoch.healthy(GetTickCount64());last_layout=status;}
        else {if(status.state==3||status.state==4) {log_line(L"taskbar layout stopped, state=%ld error=%ld",status.state,status.error);return ERROR_NOT_SUPPORTED;}if(epoch.expired(GetTickCount64(),15000)) {log_line(L"taskbar layout status timed out");return ERROR_TIMEOUT;}}
        // Layout reconstruction may briefly stop publishing geometry. Continue
        // visibility checks using the last layout instead of freezing hidden state.
        status=last_layout;RECT bounds{};if(!GetWindowRect(bar,&bounds)||bounds.right<=bounds.left||bounds.bottom<=bounds.top) continue;
        RECT strip=bounds;
        if(status.host_width>0&&status.group_left>=0&&status.group_right>status.group_left&&status.group_right<=status.host_width) {
            double scale=double(bounds.right-bounds.left)/status.host_width;strip.left=bounds.left+LONG(std::lround(status.group_left*scale));strip.right=bounds.left+LONG(std::lround(status.group_right*scale));
        }
        Coverage coverage{strip,bar};EnumWindows(covered_window,reinterpret_cast<LPARAM>(&coverage));POINT cursor{};GetCursorPos(&cursor);
        bool edge=cursor.y>=bounds.bottom-2&&cursor.y<bounds.bottom&&cursor.x>=bounds.left&&cursor.x<bounds.right;RECT hover=strip;InflateRect(&hover,5,6);
        bool reveal=reveal_at_edge(coverage.covered,edge,PtInRect(&hover,cursor)!=FALSE,visibility.revealed&&visibility.bar==bar);
        visibility.update(bar,bounds,coverage.covered&&!reveal,reveal);
    }
    return 0;
    } catch(std::exception const&) { log_line(L"taskbar helper exception; restoring through RAII");return ERROR_NOT_SUPPORTED; }
}
}
