#include "timeline.hpp"
namespace lb {
namespace {
struct Control { Timeline *model{};UINT change{};bool dragging{}; };
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto control=reinterpret_cast<Control*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) { auto create=reinterpret_cast<CREATESTRUCTW*>(lp);control=static_cast<Control*>(create->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(control)); }
    if(!control) return DefWindowProcW(window,message,wp,lp);auto &model=*control->model;RECT bounds{};GetClientRect(window,&bounds);
    auto changed=[&]{InvalidateRect(window,nullptr,FALSE);PostMessageW(GetParent(window),control->change,0,0);};
    switch(message) {
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);unique_gdi<HBRUSH> background(CreateSolidBrush(RGB(28,34,44)));FillRect(dc,&bounds,background.get());
        RECT track{12,14,bounds.right-12,32};rounded(dc,track,8,RGB(55,66,84),RGB(55,66,84));track.left=model.x(model.first,bounds.right);track.right=model.x(model.last,bounds.right);rounded(dc,track,6,RGB(78,155,142),RGB(100,217,186));
        for(auto handle:{Timeline::Handle::start,Timeline::Handle::end,Timeline::Handle::play}) {
            int x=model.x(handle==Timeline::Handle::start?model.first:handle==Timeline::Handle::end?model.last:model.position,bounds.right);
            RECT grip{x-4,handle==Timeline::Handle::play?8:11,x+4,handle==Timeline::Handle::play?37:35};rounded(dc,grip,4,model.active==handle?RGB(255,208,112):RGB(221,243,237),RGB(221,243,237));
        }
        wchar_t label[160];StringCchPrintfW(label,160,L"%.3f s  —  %.3f s   ·   %.3f s   ·   ← / →  1 frame; Shift 10",model.milliseconds(model.first)/1000.,model.milliseconds(model.last)/1000.,model.milliseconds(model.position)/1000.);
        draw_text(dc,label,RECT{12,40,bounds.right-12,66},g_app.small,RGB(229,237,246),DT_CENTER|DT_SINGLELINE);EndPaint(window,&ps);return 0;
    }
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),start=abs(x-model.x(model.first,bounds.right)),end=abs(x-model.x(model.last,bounds.right));model.active=std::min(start,end)<=12?(start<=end?Timeline::Handle::start:Timeline::Handle::end):Timeline::Handle::play;model.apply(model.frame_at(x,bounds.right));control->dragging=true;SetFocus(window);SetCapture(window);changed();return 0;
    }
    case WM_MOUSEMOVE:if(control->dragging) { model.apply(model.frame_at(GET_X_LPARAM(lp),bounds.right));changed(); }return 0;
    case WM_LBUTTONUP:control->dragging=false;ReleaseCapture();return 0;
    case WM_KEYDOWN:if(wp==VK_LEFT||wp==VK_RIGHT) { model.step(wp==VK_RIGHT,GetKeyState(VK_SHIFT)<0);changed(); }return 0;
    case WM_DESTROY:delete control;SetWindowLongPtrW(window,GWLP_USERDATA,0);return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
}
HWND timeline_create(HWND parent,RECT bounds,Timeline *model,UINT change) {
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.Timeline";cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_SIZEWE);RegisterClassW(&cls);auto control=std::make_unique<Control>();control->model=model;control->change=change;
    HWND window=CreateWindowExW(0,cls.lpszClassName,tr(L"拖动两端裁剪；方向键逐帧调整",L"Drag handles to trim; arrows move one frame"),WS_CHILD|WS_VISIBLE|WS_TABSTOP,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,parent,nullptr,g_app.instance,control.get());if(window) control.release();return window;
}
bool timeline_tests() {
    Timeline model;model.reset(10000,29.97);if(model.frames!=300||model.last!=300) return false;
    model.active=Timeline::Handle::start;model.apply(40);model.step(true,false);if(model.first!=41||model.position!=41) return false;model.step(false,true);if(model.first!=31) return false;
    model.active=Timeline::Handle::end;model.apply(-100);if(model.last!=32||model.position!=31) return false;model.active=Timeline::Handle::start;model.apply(9999);if(model.first!=31) return false;
    if(model.milliseconds(300)!=10000||model.frame_at(1012,1024)!=300) return false;model.reset(0,24);model.step(true,false);return model.first==0&&model.last==0&&model.position==0;
}
}
