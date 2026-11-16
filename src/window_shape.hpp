#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

namespace lb::window_shape {
// Custom regions/per-pixel-alpha surfaces own their frame. Do not combine
// their round mask with DWM's rectangular non-client border and shadow.
inline void own_frame(HWND window){
    auto policy=DWMNCRP_DISABLED;DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&policy,sizeof policy);
    int corner=1;DwmSetWindowAttribute(window,33,&corner,sizeof corner);
    COLORREF border=0xfffffffe;DwmSetWindowAttribute(window,34,&border,sizeof border);
}
inline bool clip(HWND window,int diameter){
    RECT bounds{};if(!GetClientRect(window,&bounds)||bounds.right<=0||bounds.bottom<=0)return false;
    diameter=std::clamp(diameter,0,int(std::min(bounds.right,bounds.bottom)));
    // Both GDI RoundRect and this region use the same exclusive right/bottom.
    HRGN region=CreateRoundRectRgn(0,0,bounds.right,bounds.bottom,diameter,diameter);
    if(!region)return false;if(SetWindowRgn(window,region,TRUE))return true;DeleteObject(region);return false;
}
inline bool tests(){
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"isolated rounded surface",WS_POPUP,0,0,600,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)return false;
    own_frame(window);HRGN observed=CreateRectRgn(0,0,0,0);bool ok=observed!=nullptr;
    for(int dpi:{96,120,144,192,240})for(int height:{84,120,700,120,84}){
        int w=MulDiv(600,dpi,96),h=MulDiv(height,dpi,96);SetWindowPos(window,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        ok=clip(window,MulDiv(26,dpi,96))&&GetWindowRgn(window,observed)==COMPLEXREGION&&ok;
        for(POINT p:{POINT{0,0},POINT{w-1,0},POINT{0,h-1},POINT{w-1,h-1},POINT{w,h/2},POINT{w/2,h}})if(PtInRegion(observed,p.x,p.y))ok=false;
        if(!PtInRegion(observed,w/2,h/2)||!PtInRegion(observed,w/2,1)||!PtInRegion(observed,1,h/2))ok=false;
    }
    if(observed)DeleteObject(observed);DestroyWindow(window);return ok;
}
}
