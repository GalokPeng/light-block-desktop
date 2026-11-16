#include "lb.h"
#include <mfapi.h>
#include <mfmediaengine.h>
#include <oleauto.h>
#include <propvarutil.h>
#include <psapi.h>
#include <filesystem>
#include "media_worker_api.h"
#include "frame_timing.hpp"

class Notify final : public IMFMediaEngineNotify {
    std::atomic<ULONG> refs_{1};
    HWND window_;
public:
    explicit Notify(HWND window) noexcept : window_(window) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) noexcept override {
        if(!out) return E_POINTER;*out=nullptr;
        if(IsEqualIID(id,IID_IUnknown)||IsEqualIID(id,IID_IMFMediaEngineNotify)) { *out=static_cast<IMFMediaEngineNotify*>(this);AddRef();return S_OK; }return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { ULONG value=--refs_;if(!value) delete this;return value; }
    HRESULT STDMETHODCALLTYPE EventNotify(DWORD event,DWORD_PTR p1,DWORD) noexcept override {
        if(event==MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE) { SetEvent(reinterpret_cast<HANDLE>(p1));return S_OK; }
        PostMessageW(window_,WM_LB_VIDEO,event,static_cast<LPARAM>(p1));return S_OK;
    }
};

struct Worker {
    HWND window,owner,embed; DWORD token;
    Image image; MediaKind kind;
    winrt::com_ptr<IMFMediaEngine> engine;winrt::com_ptr<IMFMediaEngineEx> engine_ex;
    winrt::com_ptr<IMFMediaEngineNotify> notify;
    HANDLE parent;
    HMONITOR monitor;
    BOOL desktop,ready,paused,auto_paused,mf_started,muted;
    BOOL locked,display_off;
    HMODULE mf_library;
    HMODULE wts_library;
    HPOWERNOTIFY display_notify;
    RECT monitor_rect;
    HRESULT error;
    UINT frames,changes; uint32_t last_hash;
    ULONGLONG began,frame_due{};
    bool hidden_probe{},resizing_video{};
    const wchar_t *path,*report;
};
static Worker s_worker;
static HMONITOR current_monitor(int index);
static constexpr GUID display_state={0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
static bool refresh_desktop_geometry(Worker* w) {
    auto monitor=current_monitor(g_app.settings.monitor);MONITORINFO info{sizeof info};if(!GetMonitorInfoW(monitor,&info)) return false;
    if(w->monitor!=monitor||memcmp(&w->monitor_rect,&info.rcMonitor,sizeof(RECT))) {
        w->monitor=monitor;w->monitor_rect=info.rcMonitor;auto parent=GetParent(w->window);if(!IsWindow(parent)) return false;
        POINT point{info.rcMonitor.left,info.rcMonitor.top};MapWindowPoints(nullptr,parent,&point,1);
        if(!SetWindowPos(w->window,HWND_BOTTOM,point.x,point.y,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,SWP_NOACTIVATE)) return false;
    }
    return true;
}

static void ready(Worker *w) {
    if(w->ready) return;w->ready=TRUE;
    if(w->owner) PostMessageW(w->owner,WM_LB_READY,w->token,0);
    log_line(L"media ready kind=%d elapsed=%llu",w->kind,GetTickCount64()-w->began);
}
static uint32_t image_hash(const Image *i) {
    uint32_t hash=2166136261u; size_t count=(size_t)i->width*i->height;
    for(size_t n=0;n<count;n+=LB_MAX(1,count/1024)) hash=(hash^i->pixels[n])*16777619u;
    return hash;
}
static void image_frame(Worker *w) {
    ++w->frames;uint32_t hash=image_hash(&w->image);if(hash!=w->last_hash) ++w->changes;w->last_hash=hash;
    InvalidateRect(w->window,NULL,FALSE);
    if(w->hidden_probe) ready(w);
    if(w->image.count>1 && !w->paused && !w->auto_paused) SetTimer(w->window,1,lb::frame_wait(GetTickCount64(),w->frame_due,w->image.delay),NULL);
}
static void embed_resize(Worker* w,UINT width=0,UINT height=0) {
    if(!w->embed||w->desktop||!IsWindow(w->embed)||!IsWindow(w->window))return;
    RECT host{},current{};if(!GetClientRect(w->embed,&host)||!GetWindowRect(w->window,&current))return;
    MapWindowPoints(nullptr,w->embed,reinterpret_cast<POINT*>(&current),2);
    // Some MF windowed renderers place a fitted stream at the HWND origin even
    // when pDst has a positive offset. Centre the video HWND itself instead.
    // The parent remains the full preview canvas and supplies the letterbox.
    RECT target=w->kind==MEDIA_VIDEO&&width&&height?fit_rect(host,width,height,FALSE):host;
    if(!EqualRect(&current,&target))SetWindowPos(w->window,nullptr,target.left,target.top,std::max(1L,target.right-target.left),std::max(1L,target.bottom-target.top),SWP_NOZORDER|SWP_NOACTIVATE);
}
bool media_embed_layout_tests() {
    HWND host=CreateWindowExW(0,L"STATIC",L"hidden preview geometry",WS_POPUP,0,0,1110,384,nullptr,nullptr,g_app.instance,nullptr);
    if(!host)return false;lb::scope_exit close([&]{DestroyWindow(host);});
    HWND child=CreateWindowExW(0,L"STATIC",L"hidden video surface",WS_CHILD,0,0,1110,384,host,nullptr,g_app.instance,nullptr);if(!child)return false;
    Worker fixture{};fixture.embed=host;fixture.window=child;fixture.kind=MEDIA_VIDEO;
    auto positioned=[&](RECT expected){RECT actual{};if(!GetWindowRect(child,&actual))return false;MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&actual),2);return EqualRect(&actual,&expected)!=FALSE;};
    embed_resize(&fixture,1920,1080);if(!positioned({213,0,896,384}))return false;
    SetWindowPos(host,nullptr,0,0,800,800,SWP_NOZORDER|SWP_NOACTIVATE);embed_resize(&fixture,1920,1080);if(!positioned({0,175,800,625}))return false;
    embed_resize(&fixture,1080,1920);if(!positioned({175,0,625,800}))return false;
    fixture.kind=MEDIA_GIF;embed_resize(&fixture,1920,1080);return positioned({0,0,800,800})&&!IsWindowVisible(host)&&!IsWindowVisible(child);
}
static void video_resize(Worker *w) {
    if(!w->engine_ex||w->resizing_video) return;
    w->resizing_video=true;lb::scope_exit resized([&]{w->resizing_video=false;});
    DWORD width=0,height=0;w->engine->GetNativeVideoSize(&width,&height);
    embed_resize(w,width,height);
    RECT client{};GetClientRect(w->window,&client);
    RECT dest=w->embed&&!w->desktop?client:fit_rect(client,width,height,FALSE);MFARGB black={0,0,0,255};MFVideoNormalizedRect source={0,0,1,1};
    int mode=w->desktop?g_app.settings.fit:1;
    if(mode==0 && width && height && client.right && client.bottom) {
        double ratio=(double)width/height,display=(double)client.right/client.bottom;dest=client;
        if(ratio>display) { source.left=(float)((1-display/ratio)/2);source.right=1-source.left; }
        else { source.top=(float)((1-ratio/display)/2);source.bottom=1-source.top; }
    } else if(mode==2) dest=client;
    else if(mode==3 && width && height) {
        dest.left=(client.right-(LONG)width)/2;dest.top=(client.bottom-(LONG)height)/2;dest.right=dest.left+(LONG)width;dest.bottom=dest.top+(LONG)height;
        if(dest.left<0) { source.left=(float)(-dest.left)/(float)width;source.right=1-source.left;dest.left=0;dest.right=client.right; }
        if(dest.top<0) { source.top=(float)(-dest.top)/(float)height;source.bottom=1-source.top;dest.top=0;dest.bottom=client.bottom; }
    }
    w->engine_ex->UpdateVideoStream(&source,&dest,&black);
}
static HRESULT video_open(Worker *w) {
    using StartupFn=HRESULT (WINAPI*)(ULONG,DWORD);
    using AttributesFn=HRESULT (WINAPI*)(IMFAttributes**,UINT32);
    w->mf_library=LoadLibraryExW(L"mfplat.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!w->mf_library) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    auto startup=reinterpret_cast<StartupFn>(GetProcAddress(w->mf_library,"MFStartup"));
    auto create_attributes=reinterpret_cast<AttributesFn>(GetProcAddress(w->mf_library,"MFCreateAttributes"));
    if(!startup||!create_attributes) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    HRESULT hr=startup(MF_VERSION,MFSTARTUP_FULL);if(FAILED(hr)) return hr;w->mf_started=TRUE;
    w->notify.attach(new(std::nothrow) Notify(w->window));if(!w->notify) return E_OUTOFMEMORY;
    winrt::com_ptr<IMFAttributes> attributes;winrt::com_ptr<IMFMediaEngineClassFactory> factory;
    hr=create_attributes(attributes.put(),4);if(FAILED(hr)) return hr;
    hr=attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK,w->notify.get());if(FAILED(hr)) return hr;
    hr=attributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND,reinterpret_cast<UINT64>(w->window));if(FAILED(hr)) return hr;
    hr=CoCreateInstance(CLSID_MFMediaEngineClassFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IMFMediaEngineClassFactory,factory.put_void());if(FAILED(hr)) return hr;
    hr=factory->CreateInstance(0,attributes.get(),w->engine.put());if(FAILED(hr)) return hr;
    hr=w->engine->QueryInterface(IID_IMFMediaEngineEx,w->engine_ex.put_void());if(FAILED(hr)) return hr;
    w->engine->SetMuted(TRUE);w->engine->SetLoop(TRUE);w->engine->SetAutoPlay(FALSE);
    BSTR source=SysAllocString(w->path);if(!source) return E_OUTOFMEMORY;
    lb::scope_exit free_source([&]{SysFreeString(source);});
    hr=w->engine->SetSource(source);return SUCCEEDED(hr)?w->engine->Load():hr;
}
static void playback_pause(Worker *w) {
    BOOL paused=w->paused || w->auto_paused;
    if(w->engine) { if(paused) w->engine->Pause();else w->engine->Play(); }
    else if(w->image.count>1) { if(paused) KillTimer(w->window,1);else {w->frame_due=lb::next_frame_due(GetTickCount64(),w->image.delay);SetTimer(w->window,1,w->image.delay,NULL);} }
}
static void report(Worker *w) {
    if(!w->report) return;
    UINT64 rendered=0,dropped=0;
    if(w->engine_ex) {
        PROPVARIANT v;PropVariantInit(&v);
        if(SUCCEEDED(w->engine_ex->GetStatistics(MF_MEDIA_ENGINE_STATISTIC_FRAMES_RENDERED,&v))) { if(v.vt==VT_UI4) rendered=v.ulVal;else if(v.vt==VT_UI8) rendered=v.uhVal.QuadPart; } PropVariantClear(&v);
        if(SUCCEEDED(w->engine_ex->GetStatistics(MF_MEDIA_ENGINE_STATISTIC_FRAMES_DROPPED,&v))) { if(v.vt==VT_UI4) dropped=v.ulVal;else if(v.vt==VT_UI8) dropped=v.uhVal.QuadPart; } PropVariantClear(&v);
    }
    PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof memory;GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof memory);
    wchar_t text[1500];
    StringCchPrintfW(text,1500,L"{\n  \"ready\": %s,\n  \"kind\": %d,\n  \"error\": %lu,\n  \"image_frames\": %u,\n  \"image_changes\": %u,\n  \"video_frames\": %llu,\n  \"video_dropped\": %llu,\n  \"working_set\": %llu,\n  \"private_bytes\": %llu,\n  \"elapsed_ms\": %llu\n}\n",w->ready?L"true":L"false",w->kind,(DWORD)w->error,w->frames,w->changes,rendered,dropped,(unsigned long long)memory.WorkingSetSize,(unsigned long long)memory.PrivateUsage,GetTickCount64()-w->began);
    write_utf8_atomic(w->report,text);
}
static LRESULT CALLBACK worker_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    Worker *w=&s_worker;
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT r;GetClientRect(window,&r);
        if(w->kind==MEDIA_VIDEO && w->engine_ex) w->engine_ex->UpdateVideoStream(NULL,NULL,NULL);
        else { FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));image_draw(dc,&w->image,r,w->desktop?g_app.settings.fit:1); }
        EndPaint(window,&ps);
        if(w->kind!=MEDIA_VIDEO && w->image.pixels) ready(w);
        return 0;
    }
    case WM_SIZE:video_resize(w);InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_WTSSESSION_CHANGE:if(wp==7) w->locked=TRUE;else if(wp==8) w->locked=FALSE;PostMessageW(window,WM_TIMER,2,0);return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_POWERSETTINGCHANGE&&lp) {auto setting=reinterpret_cast<POWERBROADCAST_SETTING*>(lp);if(IsEqualGUID(setting->PowerSetting,display_state)&&setting->DataLength>=sizeof(DWORD)) {DWORD state{};memcpy(&state,setting->Data,sizeof state);w->display_off=state==0;PostMessageW(window,WM_TIMER,2,0);}}
        return TRUE;
    case WM_LB_VIDEO:
        if(wp==MF_MEDIA_ENGINE_EVENT_CANPLAY) { video_resize(w);if(!w->paused && !w->auto_paused) w->engine->Play(); }
        else if(wp==MF_MEDIA_ENGINE_EVENT_FIRSTFRAMEREADY || wp==MF_MEDIA_ENGINE_EVENT_PLAYING) { video_resize(w);ready(w); }
        else if(wp==MF_MEDIA_ENGINE_EVENT_ERROR) {
            IMFMediaError *error=NULL;w->error=E_FAIL;
            if(w->engine && SUCCEEDED(w->engine->GetError(&error)) && error) { w->error=error->GetExtendedErrorCode();if(SUCCEEDED(w->error)) w->error=E_FAIL;error->Release(); }
            log_line(L"video decode failure 0x%08lX",(DWORD)w->error);
            if(w->owner) PostMessageW(w->owner,WM_LB_ERROR,w->token,(LPARAM)w->error);
            PostMessageW(window,WM_CLOSE,0,0);
        }
        return 0;
    case WM_TIMER:
        if(wp==1) {
            KillTimer(window,1);
            auto started=GetTickCount64();
            HRESULT hr=image_next(&w->image);
            w->frame_due=lb::next_frame_due(started,w->image.delay);
            if(SUCCEEDED(hr)) image_frame(w);else { w->error=hr;PostMessageW(window,WM_CLOSE,0,0); }
        } else if(wp==2) {
            if(w->parent) { DWORD state=WaitForSingleObject(w->parent,0);if(state!=WAIT_TIMEOUT) { if(state==WAIT_FAILED) w->error=HRESULT_FROM_WIN32(GetLastError());log_line(L"parent watch ended state=%lu hr=0x%08lX",state,DWORD(w->error));PostMessageW(window,WM_CLOSE,0,0);return 0; } }
            if(w->embed&&w->ready) {
                if(!IsWindow(w->embed)) { PostMessageW(window,WM_CLOSE,0,0);return 0; }
                if(w->engine) { double position=w->engine->GetCurrentTime(),duration=w->engine->GetDuration();
                    if(std::isfinite(position)&&position>=0) PostMessageW(w->owner,WM_LB_POSITION,w->token,LPARAM(int64_t(position*1000)));
                    if(std::isfinite(duration)&&duration>0) PostMessageW(w->owner,WM_LB_DURATION,w->token,LPARAM(int64_t(duration*1000))); }
                DWORD width=0,height=0;if(w->kind==MEDIA_VIDEO&&w->engine)w->engine->GetNativeVideoSize(&width,&height);embed_resize(w,width,height);
            }
            if(w->desktop) {
                if(!IsWindow(GetParent(window))||!refresh_desktop_geometry(w)) { w->error=HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);PostMessageW(window,WM_CLOSE,0,0);return 0; }
                SYSTEM_POWER_STATUS power={};BOOL on_battery=g_app.settings.battery_pause && GetSystemPowerStatus(&power) && power.ACLineStatus==0;
                BOOL paused=w->locked||w->display_off||(g_app.settings.fullscreen_pause && fullscreen_on(w->monitor,window)) || on_battery;
                if(paused!=w->auto_paused) { w->auto_paused=paused;playback_pause(w); }
            }
            if(!w->ready && GetTickCount64()-w->began>14000) { w->error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);PostMessageW(window,WM_CLOSE,0,0); }
        } else if(wp==3) PostMessageW(window,WM_CLOSE,0,0);
        return 0;
    case WM_LB_CONTROL:
        if(wp==1) { w->paused=!w->paused;playback_pause(w); }
        else if(wp==2 && w->engine) { w->muted=!w->muted;w->engine->SetMuted(w->muted); }
        else if(wp==3 && w->engine) { double duration=w->engine->GetDuration();if(std::isfinite(duration)&&duration>0) w->engine->SetCurrentTime(std::clamp(double(lp)/1000.,0.,duration)); }
        else if(wp==4) { load_config();video_resize(w);InvalidateRect(window,nullptr,FALSE); }
        else if(wp==5) { w->paused=lp!=0;playback_pause(w); }
        else if(wp==6) { if(w->engine_ex)video_resize(w);else embed_resize(w);InvalidateRect(window,nullptr,FALSE);return 0; }
        if(w->owner) PostMessageW(w->owner,WM_LB_STATE,w->token,(w->paused?1:0)|(w->muted?2:0));
        return 0;
    case WM_LBUTTONDOWN:
        if(w->embed){HWND root=GetAncestor(w->embed,GA_ROOT);wchar_t type[128]{};GetClassNameW(root,type,128);if(!wcscmp(type,L"LightBlockDesktopNative.RecordingFrame")){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(ClientToScreen(window,&point)){ReleaseCapture();PostMessageW(root,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));return 0;}}}break;
    case WM_KEYDOWN:
        if(w->embed){HWND root=GetAncestor(w->embed,GA_ROOT);wchar_t type[128]{};GetClassNameW(root,type,128);if(!wcscmp(type,L"LightBlockDesktopNative.RecordingFrame")){bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;int action=wp==VK_ESCAPE?9:wp==VK_SPACE?6:ctrl&&wp=='S'?7:ctrl&&wp=='C'?8:ctrl&&wp=='Z'?33:ctrl&&wp=='Y'?34:0;if(action){PostMessageW(root,WM_APP+84,WPARAM(action),0);return 0;}}}
        if(wp==VK_ESCAPE) PostMessageW(window,WM_CLOSE,0,0);
        else if(wp==VK_SPACE) PostMessageW(window,WM_LB_CONTROL,1,0);
        return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
struct MonitorSelection { int current,wanted;HMONITOR result; };
static BOOL CALLBACK select_monitor(HMONITOR monitor,HDC dc,LPRECT rect,LPARAM value) {
    (void)dc;(void)rect;auto *selection=reinterpret_cast<MonitorSelection*>(value);
    if(++selection->current==selection->wanted) { selection->result=monitor;return FALSE; }return TRUE;
}
static HMONITOR current_monitor(int index) {
    HMONITOR monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    if(index>0) {MonitorSelection selection{0,index,nullptr};EnumDisplayMonitors(nullptr,nullptr,select_monitor,LPARAM(&selection));if(selection.result) monitor=selection.result;}
    return monitor;
}
static int qt_media_main(Worker* w) {
    auto manifest=std::filesystem::path(g_app.directory)/L"runtime"/L"media-current.txt";
    std::unique_ptr<wchar_t,decltype(&free)> raw(read_utf8_file(manifest.c_str(),256),free);
    std::wstring name=raw?raw.get():L"";while(!name.empty()&&iswspace(name.back())) name.pop_back();
    if(name.empty()||name.find_first_of(L"\\/:")!=std::wstring::npos||!name.starts_with(L"LightBlockMedia-")||!name.ends_with(L".dll")) { w->error=HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);if(w->owner) PostMessageW(w->owner,WM_LB_ERROR,w->token,LPARAM(w->error));report(w);return 4; }
    auto path=std::filesystem::path(g_app.directory)/name;
    auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) {w->error=HRESULT_FROM_WIN32(GetLastError());if(w->owner) PostMessageW(w->owner,WM_LB_ERROR,w->token,LPARAM(w->error));report(w);return 4;}
    // The process exits immediately after this call. Keep Qt/FFmpeg loaded while
    // their shutdown callbacks run; unloading the DLL before process exit is unsafe.
    auto entry=reinterpret_cast<GetMediaWorkerExports>(GetProcAddress(module,"LightBlockMediaWorkerExports"));auto api=entry?entry():nullptr;
    if(!api||api->version!=LB_MEDIA_WORKER_ABI||api->size!=sizeof(UiMediaWorkerExports)||!api->run) {w->error=E_NOINTERFACE;if(w->owner) PostMessageW(w->owner,WM_LB_ERROR,w->token,LPARAM(w->error));report(w);return 4;}
    UiMediaRequest request{};request.version=LB_MEDIA_WORKER_ABI;request.size=sizeof request;request.path=w->path;request.owner=uintptr_t(w->owner);request.parent=uintptr_t(w->parent);request.embed=uintptr_t(w->embed);request.token=w->token;request.desktop=w->desktop;request.probe_milliseconds=w->report?4200:0;
    request.settings={g_app.settings.fit,g_app.settings.monitor,g_app.settings.fullscreen_pause,g_app.settings.battery_pause};
    request.attach=[](uintptr_t window,int monitor)->int{return desktop_attach(HWND(window),current_monitor(monitor))!=FALSE;};
    request.geometry=[](int monitor,UiMediaRect* rect)->int {if(!rect) return 0;MONITORINFO info{sizeof info};if(!GetMonitorInfoW(current_monitor(monitor),&info)) return 0;*rect={info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top};return 1;};
    request.fullscreen=[](uintptr_t window,int monitor)->int{return fullscreen_on(current_monitor(monitor),HWND(window))!=FALSE;};
    request.settings_changed=[](UiMediaSettings* settings){load_config();if(settings) *settings={g_app.settings.fit,g_app.settings.monitor,g_app.settings.fullscreen_pause,g_app.settings.battery_pause};};
    request.log=[](wchar_t const* line){log_line(L"%s",line);};
    UiMediaResult result{};int code=api->run(&request,&result);w->error=HRESULT(result.error);w->ready=result.ready;w->frames=UINT(result.frames);
    if(w->report) {
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof memory;GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof memory);
        wchar_t text[1200];StringCchPrintfW(text,ARRAYSIZE(text),L"{\"ready\":%s,\"kind\":%d,\"error\":%lu,\"image_frames\":0,\"image_changes\":0,\"video_frames\":%llu,\"video_dropped\":0,\"working_set\":%llu,\"private_bytes\":%llu,\"elapsed_ms\":%llu,\"backend\":\"qt-ffmpeg\"}\n",result.ready?L"true":L"false",int(w->kind),DWORD(w->error),result.frames,uint64_t(memory.WorkingSetSize),uint64_t(memory.PrivateUsage),result.elapsed_ms);write_utf8_atomic(w->report,text);
    }
    return code;
}
int worker_main(int argc,wchar_t **argv) {
    Worker *w=&s_worker;*w=Worker{};w->muted=TRUE;w->began=GetTickCount64();
    bool qt_media=false;
    for(int n=1;n<argc;++n) {
        if(!wcscmp(argv[n],L"--path") && n+1<argc) w->path=argv[++n];
        else if(!wcscmp(argv[n],L"--owner") && n+1<argc) w->owner=(HWND)(uintptr_t)_wcstoui64(argv[++n],NULL,10);
        else if(!wcscmp(argv[n],L"--token") && n+1<argc) w->token=wcstoul(argv[++n],NULL,10);
        else if(!wcscmp(argv[n],L"--parent-handle") && n+1<argc) w->parent=(HANDLE)(uintptr_t)_wcstoui64(argv[++n],NULL,10);
        else if(!wcscmp(argv[n],L"--report") && n+1<argc) w->report=argv[++n];
        else if(!wcscmp(argv[n],L"--embed") && n+1<argc) w->embed=reinterpret_cast<HWND>(uintptr_t(_wcstoui64(argv[++n],nullptr,10)));
        else if(!wcscmp(argv[n],L"--desktop")) w->desktop=TRUE;
        else if(!wcscmp(argv[n],L"--qt-media")) qt_media=true;
        else if(!wcscmp(argv[n],L"--hidden-probe")) w->hidden_probe=true;
    }
    if(!w->path || (!w->owner && !w->report)) return 2;
    lb::com_apartment apartment(COINIT_MULTITHREADED);
    if(FAILED(apartment.result())) return 3;
    lb::scope_exit cleanup([&]{
        if(w->engine) w->engine->Shutdown();w->engine_ex=nullptr;w->engine=nullptr;w->notify=nullptr;image_close(&w->image);
        if(w->mf_started) { using ShutdownFn=HRESULT(WINAPI*)();auto shutdown=reinterpret_cast<ShutdownFn>(GetProcAddress(w->mf_library,"MFShutdown"));if(shutdown) shutdown(); }
        if(w->mf_library) FreeLibrary(w->mf_library);if(w->parent) CloseHandle(w->parent);
        if(w->display_notify) UnregisterPowerSettingNotification(w->display_notify);
        if(w->wts_library) {using Fn=BOOL(WINAPI*)(HWND);auto fn=reinterpret_cast<Fn>(GetProcAddress(w->wts_library,"WTSUnRegisterSessionNotification"));if(fn) fn(w->window);FreeLibrary(w->wts_library);}
    });
    w->kind=media_kind(w->path);
    if(qt_media&&w->kind==MEDIA_VIDEO) return qt_media_main(w);
    WNDCLASSW cls={};cls.lpfnWndProc=worker_proc;cls.hInstance=g_app.instance;cls.lpszClassName=LB_PLAYER;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);
    RegisterClassW(&cls);
    w->monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    if(w->desktop && g_app.settings.monitor>0) { MonitorSelection selection={0,g_app.settings.monitor,NULL};EnumDisplayMonitors(NULL,NULL,select_monitor,(LPARAM)&selection);if(selection.result) w->monitor=selection.result; }
    if(!w->desktop) w->monitor=MonitorFromWindow(w->owner,MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi={sizeof mi};GetMonitorInfoW(w->monitor,&mi);
    int width=900,height=540;
    w->window=CreateWindowExW(WS_EX_TOOLWINDOW,LB_PLAYER,tr(L"轻块桌面 · 壁纸预览",L"Light Block Desktop · Preview"),WS_OVERLAPPEDWINDOW,mi.rcWork.left+60,mi.rcWork.top+60,width,height,NULL,NULL,g_app.instance,NULL);
    if(!w->window) return 3;
    if(w->desktop) {
        w->display_notify=RegisterPowerSettingNotification(w->window,&display_state,DEVICE_NOTIFY_WINDOW_HANDLE);
        w->wts_library=LoadLibraryExW(L"wtsapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(w->wts_library) {using Fn=BOOL(WINAPI*)(HWND,DWORD);auto fn=reinterpret_cast<Fn>(GetProcAddress(w->wts_library,"WTSRegisterSessionNotification"));if(fn) fn(w->window,0);}
    }
    if(w->owner) PostMessageW(w->owner,WM_LB_HELLO,w->token,(LPARAM)w->window);
    theme_window(w->window);
    if(w->embed) {
        DWORD owner_pid=0,embed_pid=0;GetWindowThreadProcessId(w->owner,&owner_pid);GetWindowThreadProcessId(w->embed,&embed_pid);
        if(!owner_pid || owner_pid!=embed_pid) { w->error=E_INVALIDARG;DestroyWindow(w->window);report(w);return 4; }
        SetWindowLongPtrW(w->window,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);SetWindowLongPtrW(w->window,GWL_EXSTYLE,(GetWindowLongPtrW(w->window,GWL_EXSTYLE)&~WS_EX_APPWINDOW)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);SetParent(w->window,w->embed);RECT client{};GetClientRect(w->embed,&client);SetWindowPos(w->window,nullptr,0,0,client.right,client.bottom,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
    }
    if(w->kind==MEDIA_VIDEO) w->error=video_open(w);
    else w->error=image_open(&w->image,w->path);
    if(SUCCEEDED(w->error) && w->desktop && !desktop_attach(w->window,w->monitor)) w->error=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if(FAILED(w->error)) {
        if(w->owner) PostMessageW(w->owner,WM_LB_ERROR,w->token,(LPARAM)w->error);
        log_line(L"media open failed hr=0x%08lX",(DWORD)w->error);
        DestroyWindow(w->window);
    } else {
        if(!w->hidden_probe) ShowWindow(w->window,SW_SHOWNOACTIVATE);
        SetTimer(w->window,2,500,NULL);
        if(w->report) SetTimer(w->window,3,4200,NULL);
        if(w->kind!=MEDIA_VIDEO) {w->frame_due=lb::next_frame_due(GetTickCount64(),w->image.delay);image_frame(w);}
    }
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg);DispatchMessageW(&msg); }
    report(w);return FAILED(w->error)?4:0;
}
