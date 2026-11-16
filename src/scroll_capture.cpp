#include "scroll_capture.hpp"
#include "annotation.hpp"
#include "frontend.hpp"
#include "scroll_ui_api.h"
#include <thread>
#include <deque>
#include <array>

namespace lb::scroll {
namespace {
constexpr UINT sample_timer=1,close_timer=2,settle_timer=3,manual_timer=4,final_timer=5;
struct SourceMonitor {std::wstring device;RECT bounds{},work{};};
std::vector<SourceMonitor> current_monitors(){
    std::vector<SourceMonitor> monitors;
    auto visit=[](HMONITOR monitor,HDC,LPRECT,LPARAM value)->BOOL{MONITORINFOEXW info{};info.cbSize=sizeof info;if(GetMonitorInfoW(monitor,&info))reinterpret_cast<std::vector<SourceMonitor>*>(value)->push_back({info.szDevice,info.rcMonitor,info.rcWork});return TRUE;};
    EnumDisplayMonitors(nullptr,nullptr,visit,LPARAM(&monitors));return monitors;
}
bool same_sources(std::vector<SourceMonitor> const& expected,std::vector<SourceMonitor> const& actual){
    return std::all_of(expected.begin(),expected.end(),[&](auto const& source){return std::any_of(actual.begin(),actual.end(),[&](auto const& now){return !_wcsicmp(source.device.c_str(),now.device.c_str())&&EqualRect(&source.bounds,&now.bounds);});});
}
std::optional<POINT> panel_position(RECT region,std::vector<RECT> const& work,SIZE panel,int gap){
    auto fits=[&](POINT p,RECT area){RECT box{p.x,p.y,p.x+panel.cx,p.y+panel.cy},overlap{},excluded=region;InflateRect(&excluded,gap,gap);return box.left>=area.left&&box.top>=area.top&&box.right<=area.right&&box.bottom<=area.bottom&&!IntersectRect(&overlap,&box,&excluded);};
    for(auto area:work){if(panel.cx>area.right-area.left||panel.cy>area.bottom-area.top)continue;LONG top=std::clamp(region.top,area.top,area.bottom-panel.cy),left=std::clamp(region.left,area.left,area.right-panel.cx);
        for(POINT p:std::array<POINT,4>{{{region.right+gap,top},{region.left-gap-panel.cx,top},{left,region.bottom+gap},{left,region.top-gap-panel.cy}}})if(fits(p,area))return p;}
    // A separate monitor may not lie directly beside any edge of the capture.
    // Its corners remain valid positions as long as the reserved card is clear.
    for(auto area:work)for(POINT p:std::array<POINT,4>{{{area.right-panel.cx,area.top},{area.left,area.top},{area.right-panel.cx,area.bottom-panel.cy},{area.left,area.bottom-panel.cy}}})if(fits(p,area))return p;
    return {};
}
struct Session {
    RECT region{};HWND window{},outline{},finish{},cancel{},label{},next{},undo{},overlap{},manual{};HHOOK mouse{};
    Stitch stitch;Frame unmatched;std::deque<Frame> queued;unique_gdi<HBITMAP> thumbnail,next_thumbnail;
    std::thread worker;std::atomic_bool done{};Stitch::Change change{Stitch::Change::unchanged};
    bool running{},finishing{},closing{},sample_pending{},resample{},excluded{},fixture{},final_pending{},source_lost{};
    std::vector<SourceMonitor> source_monitors;
    std::function<std::vector<SourceMonitor>()> monitor_source;
    std::function<Frame()> frame_source;
    std::function<void(Frame)> fixture_complete;
    int preview_height{100},preview_limit{320},next_height{100},last_window_height{},deferred_operation{},deferred_overlap{},pages{1};
    UiScrollExports const* ui{};uintptr_t panel{};uint32_t preview_revision{1};int shown_width{},shown_height{},shown_pages{1};bool can_undo_cached{},manual_cached{};
};
std::unique_ptr<Session> session;
HWND panel_window(Session const& state){return state.panel?HWND(state.panel):state.window;}
void panel_show(Session& state,int command){if(state.panel)state.ui->show(state.panel,command!=SW_HIDE);else ShowWindow(state.window,command);}
UiScrollState panel_state(Session& state){UiScrollState out{};if(!state.running){state.shown_width=state.stitch.width;state.shown_height=state.stitch.height;state.shown_pages=state.pages;}RECT rect{};GetWindowRect(panel_window(state),&rect);out.english=g_app.english;out.left=rect.left;out.top=rect.top;out.width=state.shown_width;out.height=state.shown_height;out.pages=state.shown_pages;out.frame_height=state.region.bottom-state.region.top;out.overlap=int(SendMessageW(state.overlap,CB_GETCURSEL,0,0));out.preview_height=state.preview_height;out.can_next=IsWindowEnabled(state.next);out.can_undo=IsWindowEnabled(state.undo);out.can_manual=IsWindowEnabled(state.manual);out.finishing=state.finishing;out.revision=state.preview_revision;out.thumbnail=uintptr_t(state.thumbnail.get());GetWindowTextW(state.label,out.status,256);return out;}
void publish(Session& state){if(state.panel){auto out=panel_state(state);state.ui->update(state.panel,&out);}}
void panel_callback(uintptr_t owner,int command,int value){if(session&&uintptr_t(session->window)==owner&&!session->closing)PostMessageW(session->window,WM_APP+86,WPARAM(command),LPARAM(value));}
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
BITMAPINFO bitmap_info(int width,int height) { BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;return info; }
Frame bitmap_frame(HBITMAP bitmap) {
    BITMAP dimensions{};if(!bitmap||!GetObjectW(bitmap,sizeof dimensions,&dimensions))return {};
    Frame frame;frame.width=dimensions.bmWidth;frame.height=dimensions.bmHeight;if(frame.width<32||frame.height<96||!fits_pixel_budget(frame.width,frame.height))return {};
    frame.pixels.resize(size_t(frame.width)*frame.height);auto info=bitmap_info(frame.width,frame.height);HDC dc=GetDC(nullptr);int lines=GetDIBits(dc,bitmap,0,UINT(frame.height),frame.pixels.data(),&info,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);return lines==frame.height?std::move(frame):Frame{};
}
Frame grab(RECT region) {
    int width=region.right-region.left,height=region.bottom-region.top;if(width<32||height<96||!fits_pixel_budget(width,height))return {};
    auto info=bitmap_info(width,height);void* pixels=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!bitmap)return {};
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);if(!screen||!dc){if(screen)ReleaseDC(nullptr,screen);if(dc)DeleteDC(dc);return {};}
    auto old=SelectObject(dc,bitmap.get());BOOL ok=BitBlt(dc,0,0,width,height,screen,region.left,region.top,SRCCOPY|CAPTUREBLT);SelectObject(dc,old);DeleteDC(dc);ReleaseDC(nullptr,screen);if(!ok)return {};
    return {width,height,std::vector<uint32_t>(static_cast<uint32_t*>(pixels),static_cast<uint32_t*>(pixels)+size_t(width)*height)};
}
void make_thumbnail(Session& state) {
    if(!state.stitch.width||!state.stitch.height)return;MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromRect(&state.region,MONITOR_DEFAULTTONEAREST),&monitor);
    int width=px(state.ui?272:220);state.preview_limit=std::min(px(320),std::max(px(110),int(monitor.rcWork.bottom-monitor.rcWork.top)/2));state.next_height=std::clamp(MulDiv(state.stitch.height,width,state.stitch.width),px(64),state.preview_limit);
    auto info=bitmap_info(width,state.next_height);void* bits=nullptr;state.next_thumbnail.reset(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0));if(!state.next_thumbnail)return;
    HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,state.next_thumbnail.get());RECT area{0,0,width,state.next_height};unique_gdi<HBRUSH> background(CreateSolidBrush(RGB(18,23,31)));FillRect(dc,&area,background.get());SetStretchBltMode(dc,HALFTONE);
    auto source=bitmap_info(state.stitch.width,state.stitch.height);int fitted=std::max(1,std::min(width,MulDiv(state.stitch.width,state.next_height,state.stitch.height)));StretchDIBits(dc,(width-fitted)/2,0,fitted,state.next_height,0,0,state.stitch.width,state.stitch.height,state.stitch.pixels.data(),&source,DIB_RGB_COLORS,SRCCOPY);GdiFlush();SelectObject(dc,old);DeleteDC(dc);
}
void place(Session& state) {
    if(state.panel){publish(state);return;}
    int base=state.preview_height+px(22),height=base+px(168);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromWindow(state.window,MONITOR_DEFAULTTONEAREST),&monitor);RECT rect{};GetWindowRect(state.window,&rect);
    if(height==state.last_window_height)return;
    state.last_window_height=height;int y=std::max(int(monitor.rcWork.top),std::min(int(rect.top),int(monitor.rcWork.bottom)-height));
    SetWindowPos(state.window,nullptr,rect.left,y,px(244),height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS);
    // Move all children as one transaction. Moving them one at a time with
    // repaint suppressed leaves old button pixels behind as the preview grows.
    struct Control { HWND window;int x,y,width,height; };
    Control controls[]={{state.label,12,0,220,42},{state.next,12,46,140,30},{state.undo,158,46,74,30},{state.overlap,12,82,126,220},{state.manual,144,82,88,30},{state.finish,12,122,106,30},{state.cancel,124,122,108,30}};
    HDWP batch=BeginDeferWindowPos(7);
    for(auto const& c:controls)if(batch)batch=DeferWindowPos(batch,c.window,nullptr,px(c.x),base+px(c.y),px(c.width),px(c.height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS);
    if(!batch||!EndDeferWindowPos(batch))for(auto const& c:controls)SetWindowPos(c.window,nullptr,px(c.x),base+px(c.y),px(c.width),px(c.height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_NOCOPYBITS);
    RedrawWindow(state.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void enabled(HWND control,bool value){if(bool(IsWindowEnabled(control))!=value)EnableWindow(control,value);}
void update_actions(Session& state){if(!state.running){state.can_undo_cached=state.stitch.can_undo();state.manual_cached=!state.unmatched.pixels.empty();}enabled(state.next,!state.finishing&&!state.source_lost);enabled(state.undo,!state.finishing&&state.can_undo_cached);enabled(state.manual,!state.finishing&&state.manual_cached);publish(state);}
void label_text(HWND window,wchar_t const* text){wchar_t current[180]{};GetWindowTextW(window,current,180);if(wcscmp(current,text))SetWindowTextW(window,text);}
bool check_source(Session& state){
    if(state.source_lost)return false;
    if(state.source_monitors.empty()||same_sources(state.source_monitors,state.monitor_source?state.monitor_source():current_monitors()))return true;
    // Coordinates from a disconnected/rearranged screen must never capture a
    // newly exposed desktop or another monitor. Keep the accumulated image.
    state.source_lost=true;state.sample_pending=state.resample=state.final_pending=false;
    for(UINT_PTR timer:{sample_timer,settle_timer,manual_timer,final_timer})KillTimer(state.window,timer);
    if(state.mouse)UnhookWindowsHookEx(std::exchange(state.mouse,nullptr));
    ShowWindow(state.outline,SW_HIDE);if(!state.fixture&&!state.closing)panel_show(state,SW_SHOWNOACTIVATE);
    label_text(state.label,tr(L"原显示器已断开或布局改变。已采集内容可点完成继续编辑。",L"The source display changed. Finish to edit the captured image."));update_actions(state);return false;
}
void start_job(Session& state,Frame frame,int operation=0,int overlap=0) {
    if(state.running||state.closing)return;state.running=true;state.done=false;
    state.worker=std::thread([&state,frame=std::move(frame),operation,overlap]()mutable{
        try{
            if(operation==2){state.unmatched=state.stitch.current_frame();state.change=state.stitch.undo()?Stitch::Change::added:Stitch::Change::unchanged;if(state.change==Stitch::Change::added)state.pages=std::max(1,state.pages-1);}
            else{state.change=operation==1?state.stitch.manual(frame,overlap):state.stitch.append(frame);if(state.change==Stitch::Change::unmatched)state.unmatched=std::move(frame);else if(state.change!=Stitch::Change::limit)state.unmatched={};}
            if(state.change==Stitch::Change::added){if(operation!=2)++state.pages;make_thumbnail(state);}
        }catch(...){state.change=Stitch::Change::limit;}
        state.done=true;PostMessageW(state.window,WM_APP+84,0,0);
    });
}
bool overlaps_panel(Session const& state){RECT panel{},intersection{};GetWindowRect(panel_window(state),&panel);return IntersectRect(&intersection,&panel,&state.region)!=FALSE;}
void sample(Session& state) {
    if(state.closing||state.finishing||state.deferred_operation||!check_source(state))return;
    if(!state.excluded&&IsWindowVisible(panel_window(state))&&overlaps_panel(state)){SetWindowTextW(state.label,tr(L"请移开预览，或点击采集下一屏。",L"Move this panel aside, or use Capture next."));publish(state);return;}
    auto frame=state.frame_source?state.frame_source():grab(state.region);if(frame.pixels.empty()){SetWindowTextW(state.label,tr(L"选区捕获失败，请重新框选。",L"Capture failed; please select the region again."));publish(state);return;}
    // Preserve ordered intermediate frames without unbounded screen-sized queues.
    if(state.running){if(state.queued.size()<2)state.queued.push_back(std::move(frame));else state.resample=true;return;}
    start_job(state,std::move(frame));
}
LRESULT CALLBACK wheel_hook(int code,WPARAM wp,LPARAM lp){if(code==HC_ACTION&&wp==WM_MOUSEWHEEL&&session&&!session->finishing&&!session->closing&&!session->source_lost){auto mouse=reinterpret_cast<MSLLHOOKSTRUCT*>(lp);if(PtInRect(&session->region,mouse->pt))PostMessageW(session->window,WM_APP+85,0,0);}return CallNextHookEx(session?session->mouse:nullptr,code,wp,lp);}
bool final_sample(Session& state){
    state.final_pending=false;if(!check_source(state))return true;auto frame=state.frame_source?state.frame_source():grab(state.region);
    if(frame.width!=state.region.right-state.region.left||frame.pixels.empty()){state.finishing=false;SetWindowTextW(state.label,tr(L"最后一屏未能读取，请再点击完成。",L"Could not read the last screen. Try Finish again."));EnableWindow(state.finish,TRUE);EnableWindow(state.next,TRUE);publish(state);return false;}
    // Up to two ordered intermediate frames plus this one final frame. Do not
    // discard pending overlaps when Finish races the worker or wheel settle.
    state.queued.push_back(std::move(frame));return true;
}
void finish(Session& state){
    if(state.closing)return;
    if(!state.finishing){check_source(state);state.finishing=true;state.resample=state.sample_pending=false;for(UINT_PTR timer:{sample_timer,settle_timer,manual_timer})KillTimer(state.window,timer);EnableWindow(state.finish,FALSE);EnableWindow(state.next,FALSE);EnableWindow(state.undo,FALSE);EnableWindow(state.manual,FALSE);publish(state);
        if(!state.source_lost&&!state.excluded&&IsWindowVisible(panel_window(state))&&overlaps_panel(state)){state.final_pending=true;panel_show(state,SW_HIDE);ShowWindow(state.outline,SW_HIDE);SetTimer(state.window,final_timer,70,nullptr);return;}if(!final_sample(state))return;}
    if(state.running||state.final_pending)return;if(!state.queued.empty()){auto next=std::move(state.queued.front());state.queued.pop_front();start_job(state,std::move(next));return;}
    if(state.source_lost)state.unmatched={};
    if(!state.unmatched.pixels.empty()){state.finishing=false;EnableWindow(state.finish,TRUE);update_actions(state);if(!state.fixture){panel_show(state,SW_SHOWNOACTIVATE);ShowWindow(state.outline,SW_SHOWNOACTIVATE);}return;}
    if(state.stitch.pixels.empty()){shutdown();return;}
    if(state.fixture){auto callback=std::move(state.fixture_complete);Frame result{state.stitch.width,state.stitch.height,std::move(state.stitch.pixels)};shutdown();if(callback)callback(std::move(result));return;}
    auto info=bitmap_info(state.stitch.width,state.stitch.height);void* pixels=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(bitmap)memcpy(pixels,state.stitch.pixels.data(),state.stitch.pixels.size()*sizeof(uint32_t));RECT location=state.region;shutdown();if(bitmap)capture_pin_bitmap(bitmap.release(),location,true,true,false);
}
void consume(Session& state){
    if(!state.running||!state.done)return;state.worker.join();state.running=false;if(state.closing){shutdown();return;}
    wchar_t label[180];if(state.source_lost)StringCchCopyW(label,180,tr(L"原显示器已断开或布局改变。已采集内容可点完成继续编辑。",L"The source display changed. Finish to edit the captured image."));else if(state.change==Stitch::Change::unmatched)StringCchCopyW(label,180,tr(L"往回滚一点继续匹配，或选择重叠高度后手动拼接。",L"Scroll back slightly, or choose the overlap and stitch manually."));else if(state.change==Stitch::Change::limit)StringCchCopyW(label,180,tr(L"已达到长图尺寸上限，请完成后继续。",L"Image size limit reached. Finish, then start another capture."));else StringCchPrintfW(label,180,tr(L"已采集 %d 屏 · %d × %d px\n在选区内继续滚动",L"%d screens · %d × %d px\nScroll inside the region"),state.pages,state.stitch.width,state.stitch.height);label_text(state.label,label);
    if(state.change==Stitch::Change::added&&state.next_thumbnail){++state.preview_revision;state.thumbnail=std::move(state.next_thumbnail);state.preview_height=state.next_height;place(state);RECT preview{px(12),px(12),px(232),px(12)+state.preview_height};InvalidateRect(state.window,&preview,FALSE);}
    update_actions(state);
    if(state.finishing){finish(state);return;}
    if(auto operation=std::exchange(state.deferred_operation,0)){state.queued.clear();state.resample=false;if(operation==2&&state.stitch.can_undo()){start_job(state,{},2);return;}if(operation==1&&!state.unmatched.pixels.empty()){start_job(state,std::move(state.unmatched),1,state.deferred_overlap);return;}}
    if(!state.queued.empty()){auto frame=std::move(state.queued.front());state.queued.pop_front();start_job(state,std::move(frame));return;}if(state.resample){state.resample=false;sample(state);return;}
}
LRESULT CALLBACK outline_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){if(message==WM_NCHITTEST)return HTTRANSPARENT;if(message==WM_ERASEBKGND)return 1;if(message==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));unique_gdi<HPEN> pen(CreatePen(PS_SOLID,2,RGB(90,189,250)));auto p=SelectObject(dc,pen.get()),b=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,1,1,r.right-1,r.bottom-1);SelectObject(dc,p);SelectObject(dc,b);EndPaint(window,&ps);return 0;}return DefWindowProcW(window,message,wp,lp);}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(!session)return DefWindowProcW(window,message,wp,lp);auto& state=*session;
    switch(message){
    case WM_CREATE:{state.window=window;auto control=[&](wchar_t const* cls,wchar_t const* text,int id,DWORD style){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);};
        wchar_t initial[128];StringCchPrintfW(initial,128,tr(L"已采集 1 屏 · %d × %d px\n在选区内继续滚动",L"1 screen · %d × %d px\nScroll inside the region"),state.stitch.width,state.stitch.height);state.label=control(L"STATIC",initial,0,0);state.next=control(L"BUTTON",tr(L"采集下一屏",L"Capture next"),3,WS_TABSTOP);state.undo=control(L"BUTTON",tr(L"撤销",L"Undo"),4,WS_TABSTOP);state.manual=control(L"BUTTON",tr(L"手动拼接",L"Stitch"),5,WS_TABSTOP);state.finish=control(L"BUTTON",tr(L"完成编辑",L"Finish"),1,WS_TABSTOP);state.cancel=control(L"BUTTON",tr(L"取消",L"Cancel"),2,WS_TABSTOP);state.overlap=control(L"COMBOBOX",L"",6,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        int height=state.region.bottom-state.region.top;SendMessageW(state.overlap,CB_INITSTORAGE,height,LPARAM(size_t(height)*48));
        for(int value=0;value<height;++value){wchar_t text[60];StringCchPrintfW(text,60,tr(L"重叠 %d px",L"Overlap %d px"),value);int index=int(SendMessageW(state.overlap,CB_ADDSTRING,0,LPARAM(text)));if(index>=0)SendMessageW(state.overlap,CB_SETITEMDATA,index,value);}SendMessageW(state.overlap,CB_SETCURSEL,height/2,0);
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,WPARAM(g_app.small),TRUE);EnableWindow(state.manual,FALSE);EnableWindow(state.undo,FALSE);if(!state.fixture)RegisterHotKey(window,1,MOD_NOREPEAT,VK_ESCAPE);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC output=BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);HDC dc=CreateCompatibleDC(output);unique_gdi<HBITMAP> buffer(CreateCompatibleBitmap(output,client.right,client.bottom));auto old=SelectObject(dc,buffer.get());unique_gdi<HBRUSH> brush(CreateSolidBrush(RGB(27,34,44)));FillRect(dc,&client,brush.get());if(state.thumbnail){HDC source=CreateCompatibleDC(dc);auto previous=SelectObject(source,state.thumbnail.get());BitBlt(dc,px(12),px(12),px(220),state.preview_height,source,0,0,SRCCOPY);SelectObject(source,previous);DeleteDC(source);}BitBlt(output,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);SelectObject(dc,old);DeleteDC(dc);EndPaint(window,&ps);return 0;}
    case WM_DISPLAYCHANGE:if(!state.closing)check_source(state);return 0;
    case WM_APP+85:if(!state.finishing&&!state.closing&&!state.source_lost){if(!state.sample_pending){state.sample_pending=true;SetTimer(window,sample_timer,105,nullptr);}SetTimer(window,settle_timer,190,nullptr);}return 0;
    case WM_TIMER:
        if(wp==sample_timer){KillTimer(window,sample_timer);state.sample_pending=false;sample(state);}
        else if(wp==settle_timer){KillTimer(window,settle_timer);sample(state);}
        else if(wp==manual_timer){KillTimer(window,manual_timer);if(!state.closing&&!state.finishing){sample(state);if(!state.fixture){panel_show(state,SW_SHOWNOACTIVATE);if(!state.source_lost)ShowWindow(state.outline,SW_SHOWNOACTIVATE);}}}
        else if(wp==final_timer){KillTimer(window,final_timer);if(!state.closing&&state.finishing&&final_sample(state)){finish(state);}else if(!state.closing){panel_show(state,SW_SHOWNOACTIVATE);ShowWindow(state.outline,SW_SHOWNOACTIVATE);}}
        else if(wp==close_timer&&(!state.running||state.done))shutdown();return 0;
    case WM_APP+84:consume(state);return 0;
    case WM_APP+86:if(!state.closing&&wp>=ScrollFinish&&wp<=ScrollManual){if(wp==ScrollManual)SendMessageW(state.overlap,CB_SETCURSEL,std::clamp<int>(int(lp),0,state.region.bottom-state.region.top-1),0);SendMessageW(window,WM_COMMAND,wp,0);}return 0;
    case WM_COMMAND:if(state.closing)return 0;switch(LOWORD(wp)){case 1:finish(state);break;case 2:shutdown();break;case 3:if(state.source_lost||state.finishing)break;if(!state.excluded&&overlaps_panel(state)){panel_show(state,SW_HIDE);ShowWindow(state.outline,SW_HIDE);SetTimer(window,manual_timer,70,nullptr);}else sample(state);break;case 4:if(state.finishing)break;if(state.running)state.deferred_operation=2;else if(state.stitch.can_undo())start_job(state,{},2);break;case 5:if(state.finishing)break;{int index=int(SendMessageW(state.overlap,CB_GETCURSEL,0,0)),overlap=int(SendMessageW(state.overlap,CB_GETITEMDATA,index,0));if(state.running){state.deferred_operation=1;state.deferred_overlap=overlap;}else if(!state.unmatched.pixels.empty())start_job(state,std::move(state.unmatched),1,overlap);}break;}return 0;
    case WM_NCHITTEST:{auto hit=DefWindowProcW(window,message,wp,lp);return hit==HTCLIENT?HTCAPTION:hit;}
    case WM_HOTKEY:case WM_CLOSE:shutdown();return 0;
    case WM_CTLCOLORSTATIC:SetTextColor(HDC(wp),RGB(230,239,247));SetBkColor(HDC(wp),RGB(27,34,44));SetDCBrushColor(HDC(wp),RGB(27,34,44));return LRESULT(GetStockObject(DC_BRUSH));
    }return DefWindowProcW(window,message,wp,lp);
}
}
bool start(RECT region,HBITMAP first){
    if(session||region.right-region.left<32||region.bottom-region.top<96)return false;auto frame=first?bitmap_frame(first):grab(region);if(frame.width!=region.right-region.left||frame.height!=region.bottom-region.top)return false;
    auto state=std::make_unique<Session>();state->ui=frontend::scroll_ui();state->region=region;for(auto const& monitor:current_monitors()){RECT overlap{};if(IntersectRect(&overlap,&region,&monitor.bounds))state->source_monitors.push_back(monitor);}if(state->source_monitors.empty())return false;if(state->stitch.append(std::move(frame))!=Stitch::Change::added)return false;make_thumbnail(*state);state->thumbnail=std::move(state->next_thumbnail);state->preview_height=state->next_height;session=std::move(state);
    MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromRect(&region,MONITOR_DEFAULTTONEAREST),&monitor);std::vector<RECT> work{monitor.rcWork};for(auto const& display:current_monitors())if(!EqualRect(&display.work,&monitor.rcWork))work.push_back(display.work);
    auto position=panel_position(region,work,SIZE{px(session->ui?310:244),session->preview_limit+px(session->ui?250:190)},px(22));POINT fallback{std::max(monitor.rcWork.left,monitor.rcWork.right-px(258)),monitor.rcWork.top+px(14)};int x=position?position->x:fallback.x,y=position?position->y:fallback.y;
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.LongCapture";cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,tr(L"长截图",L"Scrolling capture"),WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,x,y,px(244),px(200),g_app.window,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}
    if(session->ui){auto out=panel_state(*session);session->panel=session->ui->create(uintptr_t(window),panel_callback,&out);if(!session->panel)session->ui=nullptr;}
    cls.lpfnWndProc=outline_proc;cls.lpszClassName=L"LightBlockDesktopNative.LongCaptureOutline";RegisterClassW(&cls);session->outline=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_LAYERED,cls.lpszClassName,L"",WS_POPUP,region.left-2,region.top-2,region.right-region.left+4,region.bottom-region.top+4,window,nullptr,g_app.instance,nullptr);if(session->outline){SetLayeredWindowAttributes(session->outline,RGB(0,0,0),255,LWA_COLORKEY);SetWindowDisplayAffinity(session->outline,0x11);ShowWindow(session->outline,SW_SHOWNOACTIVATE);}
    session->excluded=SetWindowDisplayAffinity(panel_window(*session),0x11)!=FALSE;place(*session);session->mouse=SetWindowsHookExW(WH_MOUSE_LL,wheel_hook,g_app.instance,0);panel_show(*session,SW_SHOWNOACTIVATE);return true;
}
bool shutdown(){if(!session)return true;auto& state=*session;state.closing=true;state.finishing=false;state.queued.clear();if(state.mouse)UnhookWindowsHookEx(std::exchange(state.mouse,nullptr));UnregisterHotKey(state.window,1);for(UINT_PTR id:{sample_timer,settle_timer,manual_timer,final_timer})KillTimer(state.window,id);panel_show(state,SW_HIDE);ShowWindow(state.outline,SW_HIDE);
    if(state.running&&!state.done){SetTimer(state.window,close_timer,50,nullptr);return false;}if(state.worker.joinable())state.worker.join();if(state.outline)DestroyWindow(state.outline);if(state.panel){state.ui->destroy(state.panel);state.panel=0;}DestroyWindow(state.window);session.reset();return true;
}
bool session_tests(){
    if(session)return false;Frame canvas;canvas.width=160;canvas.height=4096;canvas.pixels.resize(size_t(canvas.width)*canvas.height);uint32_t random=71;for(auto& p:canvas.pixels){random=random*1664525+1013904223;p=0xff000000|(random>>8);}
    auto frame=[&](int offset){Frame f;f.width=160;f.height=240;f.pixels.assign(canvas.pixels.begin()+offset*160,canvas.pixels.begin()+(offset+240)*160);return f;};int samples=0;auto state=std::make_unique<Session>();state->fixture=true;state->excluded=true;state->region={0,0,160,240};state->frame_source=[&]{return frame(++samples*64);};if(state->stitch.append(frame(0))!=Stitch::Change::added)return false;session=std::move(state);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.ScrollFixture";cls.hInstance=g_app.instance;RegisterClassW(&cls);HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden scrolling fixture",WS_POPUP|WS_CLIPCHILDREN,0,0,244,500,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}session->window=window;
    scope_exit cleanup([&]{shutdown();auto end=GetTickCount64()+3000;while(session&&GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE))DispatchMessageW(&msg);Sleep(5);}if(session){if(session->worker.joinable())session->worker.join();session->running=false;shutdown();}});
    auto pump=[&]{MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE))DispatchMessageW(&msg);};auto began=GetTickCount64(),wheel_at=began;
    while(GetTickCount64()-began<900){auto now=GetTickCount64();if(now>=wheel_at){SendMessageW(window,WM_APP+85,0,0);wheel_at=now+20;}pump();Sleep(5);}
    int during=samples;if(during<5)return false;auto until=GetTickCount64()+600;while(GetTickCount64()<until){pump();Sleep(5);}if(session->running||samples<=during||session->stitch.height!=240+samples*64||IsWindowVisible(window))return false;
    auto before=session->stitch.height;SendMessageW(window,WM_COMMAND,4,0);until=GetTickCount64()+1000;while(session->running&&GetTickCount64()<until){pump();Sleep(5);}if(session->running||session->stitch.height>=before||session->unmatched.pixels.empty())return false;
    // Closing during work must wait for that worker without delivering another
    // capture or finishing a pin after the cancellation request.
    session->running=true;session->done=false;Session* pending=session.get();session->worker=std::thread([pending]{Sleep(70);pending->done=true;PostMessageW(pending->window,WM_APP+84,0,0);});if(shutdown())return false;until=GetTickCount64()+1000;while(session&&GetTickCount64()<until){pump();Sleep(5);}return !session&&!IsWindow(window);
}
bool finish_tests(){
    if(session)return false;Frame canvas;canvas.width=160;canvas.height=768;canvas.pixels.resize(size_t(canvas.width)*canvas.height);uint32_t random=9301;for(auto& p:canvas.pixels){random=random*1664525+1013904223;p=0xff000000|(random>>8);}
    auto frame=[&](int offset){Frame f;f.width=160;f.height=240;f.pixels.assign(canvas.pixels.begin()+offset*160,canvas.pixels.begin()+(offset+240)*160);return f;};
    Frame completed;int final_samples=0;auto state=std::make_unique<Session>();state->fixture=state->excluded=true;state->region={0,0,160,240};state->frame_source=[&]{++final_samples;return frame(256);};state->fixture_complete=[&](Frame value){completed=std::move(value);};if(state->stitch.append(frame(0))!=Stitch::Change::added)return false;session=std::move(state);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.ScrollFinishFixture";cls.hInstance=g_app.instance;RegisterClassW(&cls);HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden finish fixture",WS_POPUP|WS_CLIPCHILDREN,0,0,244,500,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}session->window=window;
    scope_exit cleanup([&]{if(session){shutdown();if(session){if(session->worker.joinable())session->worker.join();session->running=false;shutdown();}}});
    start_job(*session,frame(64));session->queued.push_back(frame(128));session->queued.push_back(frame(192));session->sample_pending=true;finish(*session);
    auto end=GetTickCount64()+4000;while(session&&GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE))DispatchMessageW(&msg);Sleep(5);}
    if(session||IsWindow(window)||final_samples!=1||completed.width!=160||completed.height!=496)return false;
    return completed.pixels.size()==size_t(160*496)&&std::equal(completed.pixels.begin(),completed.pixels.end(),canvas.pixels.begin());
}
bool controls_tests(){
    const SIZE reserved{244,510};const RECT full{0,0,1920,1080};
    auto right=panel_position({100,200,900,800},{full},reserved,22),left=panel_position({1500,200,1900,800},{full},reserved,22),below=panel_position({0,0,1920,400},{full},reserved,22),above=panel_position({0,600,1920,1080},{full},reserved,22),other=panel_position(full,{full,{-1920,-1080,0,0}},reserved,22);
    if(!right||right->x!=922||!left||left->x!=1234||!below||below->y!=422||!above||above->y!=68||!other||other->x>=0||other->y>=0||panel_position(full,{full},reserved,22))return false;
    if(session)return false;auto original_dpi=g_app.dpi;g_app.dpi=96;scope_exit restore_dpi([&]{g_app.dpi=original_dpi;});auto state=std::make_unique<Session>();state->fixture=state->excluded=true;state->region={0,0,160,240};Frame canvas{160,512,{}};canvas.pixels.resize(160*512);uint32_t seed=6329;for(auto& pixel:canvas.pixels){seed=seed*1664525+1013904223;pixel=0xff000000|(seed>>8);}auto frame=[&](int offset){return Frame{160,240,std::vector<uint32_t>(canvas.pixels.begin()+offset*160,canvas.pixels.begin()+(offset+240)*160)};};state->stitch.append(frame(0));state->stitch.append(frame(64));session=std::move(state);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.ScrollControlsFixture";RegisterClassW(&cls);auto window=CreateWindowExW(0,cls.lpszClassName,L"hidden scroll controls",WS_POPUP|WS_CLIPCHILDREN,0,0,244,500,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}
    scope_exit cleanup([&]{if(session){shutdown();if(session){if(session->worker.joinable())session->worker.join();session->running=false;shutdown();}}});
    auto pump=[&]{auto until=GetTickCount64()+3000;while(session&&session->running&&GetTickCount64()<until){MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE))DispatchMessageW(&msg);Sleep(2);}return session&&!session->running;};
    struct Events{int position{},enable{};} events;
    auto count=[](HWND child,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)->LRESULT{auto& e=*reinterpret_cast<Events*>(data);if(message==WM_WINDOWPOSCHANGED)++e.position;if(message==WM_ENABLE)++e.enable;return DefSubclassProc(child,message,wp,lp);};
    HWND children[]={session->label,session->next,session->undo,session->overlap,session->manual,session->finish,session->cancel};for(auto child:children)SetWindowSubclass(child,count,72,reinterpret_cast<DWORD_PTR>(&events));scope_exit unhook([&]{for(auto child:children)if(IsWindow(child))RemoveWindowSubclass(child,count,72);});
    for(int height:{64,100,180,280,420}){session->preview_height=px(height);place(*session);RECT client{};GetClientRect(window,&client);std::vector<RECT> rectangles;for(auto child:children){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);if(r.left<0||r.top<session->preview_height+px(20)||r.right>client.right||r.bottom>client.bottom)return false;for(auto const& previous:rectangles){RECT overlap{};if(IntersectRect(&overlap,&r,&previous))return false;}rectangles.push_back(r);}int before=events.position;for(int n=0;n<48;++n)place(*session);if(events.position!=before)return false;}
    update_actions(*session);events.enable=0;for(int n=0;n<16;++n){start_job(*session,frame(64));if(!pump())return false;}if(events.enable!=0||!IsWindowEnabled(session->undo))return false;
    start_job(*session,frame(128));SendMessageW(window,WM_COMMAND,4,0);if(session->deferred_operation!=2||!pump()||session->stitch.height!=304)return false;
    if((GetWindowLongPtrW(session->overlap,GWL_STYLE)&CBS_DROPDOWNLIST)!=CBS_DROPDOWNLIST||SendMessageW(session->overlap,CB_GETCOUNT,0,0)!=240)return false;
    for(int value:{0,1,63,64,127,187,239})if(SendMessageW(session->overlap,CB_GETITEMDATA,value,0)!=value)return false;
    session->unmatched=frame(128);int height=session->stitch.height,pages=session->pages;SendMessageW(session->overlap,CB_SETCURSEL,187,0);SendMessageW(window,WM_COMMAND,5,0);
    if(!pump()||session->stitch.height!=height+53||!session->unmatched.pixels.empty()||session->pages!=pages+1||session->preview_height>px(320))return false;
    for(auto child:children)RemoveWindowSubclass(child,count,72);return !IsWindowVisible(window);
}
bool source_tests(){
    if(session)return false;SourceMonitor first{L"fixture-left",{-1920,0,0,1080}},second{L"fixture-right",{0,0,1920,1080}},changed{L"fixture-left",{-1600,0,0,900}};
    if(!same_sources({first},{second,first})||same_sources({first},{second})||same_sources({first},{changed,second}))return false;
    Frame canvas{160,512,{}};canvas.pixels.resize(160*512);uint32_t seed=6139;for(auto& pixel:canvas.pixels){seed=seed*1664525+1013904223;pixel=0xff000000|(seed>>8);}auto frame=[&](int offset){return Frame{160,240,std::vector<uint32_t>(canvas.pixels.begin()+offset*160,canvas.pixels.begin()+(offset+240)*160)};};
    int reads=0;Frame completed;std::vector<SourceMonitor> available{first,second};auto state=std::make_unique<Session>();state->fixture=state->excluded=true;state->region={-1800,40,-1640,280};state->source_monitors={first};state->monitor_source=[&]{return available;};state->frame_source=[&]{++reads;return frame(128);};state->fixture_complete=[&](Frame value){completed=std::move(value);};state->stitch.append(frame(0));session=std::move(state);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.ScrollSourceFixture";RegisterClassW(&cls);auto window=CreateWindowExW(0,cls.lpszClassName,L"hidden source fixture",WS_POPUP|WS_CLIPCHILDREN,0,0,244,500,nullptr,nullptr,g_app.instance,nullptr);if(!window){session.reset();return false;}
    scope_exit cleanup([&]{if(session){shutdown();if(session){if(session->worker.joinable())session->worker.join();session->running=false;shutdown();}}});
    SendMessageW(window,WM_DISPLAYCHANGE,0,0);if(session->source_lost)return false;
    start_job(*session,frame(64));available={second};SendMessageW(window,WM_DISPLAYCHANGE,0,0);SendMessageW(window,WM_APP+85,0,0);sample(*session);
    if(!session->source_lost||session->sample_pending||session->resample||IsWindowEnabled(session->next)||reads)return false;
    finish(*session);auto end=GetTickCount64()+3000;while(session&&GetTickCount64()<end){MSG msg{};while(PeekMessageW(&msg,window,0,0,PM_REMOVE))DispatchMessageW(&msg);Sleep(2);}
    return !session&&!IsWindow(window)&&!reads&&completed.width==160&&completed.height==304&&std::equal(completed.pixels.begin(),completed.pixels.end(),canvas.pixels.begin());
}
bool ui_bridge_tests(){
    auto saved_dpi=g_app.dpi;g_app.dpi=96;scope_exit restore_dpi([&]{g_app.dpi=saved_dpi;});
    if(session){ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }struct Seen{int updates{},destroyed{},visibility{},width{},height{},pages{};bool copied{},finishing{};uint32_t revision{};};static Seen* observed{};Seen seen;observed=&seen;
    UiScrollExports bridge{LB_SCROLL_UI_ABI,sizeof(UiScrollExports),nullptr,+[](uintptr_t,UiScrollState const* state){++observed->updates;observed->width=state->width;observed->height=state->height;observed->pages=state->pages;observed->finishing=state->finishing;if(state->revision!=observed->revision){observed->revision=state->revision;unique_gdi<HBITMAP> copied(HBITMAP(CopyImage(HBITMAP(state->thumbnail),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION)));BITMAP info{};observed->copied=copied&&GetObjectW(copied.get(),sizeof info,&info)&&info.bmWidth>0;}},+[](uintptr_t,int){++observed->visibility;},+[](uintptr_t panel){++observed->destroyed;DestroyWindow(HWND(panel));},nullptr};
    Frame canvas{160,512,{}};canvas.pixels.resize(160*512);uint32_t random=331;for(auto& value:canvas.pixels){random=random*1664525+1013904223;value=0xff000000|(random>>8);}auto frame=[&](int top){return Frame{160,240,std::vector<uint32_t>(canvas.pixels.begin()+top*160,canvas.pixels.begin()+(top+240)*160)};};
    auto state=std::make_unique<Session>();state->fixture=state->excluded=true;state->ui=&bridge;state->region={0,0,160,240};state->stitch.append(frame(0));state->frame_source=[&]{return frame(53);};make_thumbnail(*state);state->thumbnail=std::move(state->next_thumbnail);state->preview_height=state->next_height;session=std::move(state);WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.ScrollUiBridgeFixture";RegisterClassW(&cls);auto controller=CreateWindowExW(0,cls.lpszClassName,L"hidden scroll controller",WS_POPUP,0,0,244,500,nullptr,nullptr,g_app.instance,nullptr);if(!controller){session.reset();observed=nullptr;{ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }}session->panel=uintptr_t(CreateWindowExW(0,L"STATIC",L"hidden borrowed panel",WS_POPUP,0,0,310,500,nullptr,nullptr,g_app.instance,nullptr));if(!session->panel){DestroyWindow(controller);session.reset();observed=nullptr;{ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }}
    scope_exit cleanup([&]{if(session){shutdown();if(session){if(session->worker.joinable())session->worker.join();session->running=false;shutdown();}}observed=nullptr;});auto pump=[&]{auto until=GetTickCount64()+3000;do{MSG msg{};while(PeekMessageW(&msg,controller,0,0,PM_REMOVE))DispatchMessageW(&msg);if(!session||!session->running)break;Sleep(2);}while(GetTickCount64()<until);};publish(*session);if(!seen.copied||seen.width!=160||seen.height!=240){ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }
    session->unmatched=frame(53);update_actions(*session);panel_callback(uintptr_t(controller),ScrollManual,187);pump();if(!session||session->running||session->stitch.height!=293||seen.height!=293||seen.pages!=2||seen.revision!=2){ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }
    int unchanged=seen.revision;for(int n=0;n<20;++n)publish(*session);if(seen.revision!=uint32_t(unchanged)||seen.visibility){ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }panel_callback(uintptr_t(controller),ScrollUndo,0);pump();if(!session||session->stitch.height!=240||seen.pages!=1){ log_line(L"scroll_capture regression failed at line %d",__LINE__);return false; }auto panel=HWND(session->panel);panel_callback(uintptr_t(controller),ScrollCancel,0);pump();return !session&&!IsWindow(controller)&&!IsWindow(panel)&&seen.destroyed==1&&seen.visibility==1;
}
}
