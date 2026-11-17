#pragma once
#include <windows.h>
namespace lb::desktop_layer {
inline HWND host(){
    HWND result{};EnumWindows([](HWND window,LPARAM parameter)->BOOL{
        if(FindWindowExW(window,nullptr,L"SHELLDLL_DefView",nullptr)){*reinterpret_cast<HWND*>(parameter)=window;return FALSE;}return TRUE;
    },LPARAM(&result));return result?result:GetShellWindow();
}
// HWND_BOTTOM can be *below* Explorer's WorkerW. Place desktop tools directly
// above the icon host, while retaining normal application windows above them.
inline HWND anchor(HWND window,HWND desktop){
    if(!desktop)return HWND_BOTTOM;
    HWND after=GetWindow(desktop,GW_HWNDPREV);
    if(after==window)after=GetWindow(window,GW_HWNDPREV);
    if(after&&(GetWindowLongPtrW(after,GWL_EXSTYLE)&WS_EX_TOPMOST))return HWND_NOTOPMOST;
    return after?after:HWND_TOP;
}
inline void protect_show(WINDOWPOS& position,HWND desktop){
    if(!(position.flags&SWP_SHOWWINDOW))return;
    position.hwndInsertAfter=anchor(position.hwnd,desktop);
    position.flags=(position.flags&~SWP_NOZORDER)|SWP_NOACTIVATE;
}
// The organizer is a desktop surface throughout its lifetime. Owner-window
// activation and programmatic raises do not necessarily include SHOWWINDOW.
inline void protect_position(WINDOWPOS& position,HWND desktop){
    if((position.flags&SWP_NOZORDER)&&!(position.flags&SWP_SHOWWINDOW))return;
    position.hwndInsertAfter=anchor(position.hwnd,desktop);
    position.flags=(position.flags&~SWP_NOZORDER)|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
}
inline void place(HWND window,int x,int y,int width,int height,UINT flags){
    SetWindowPos(window,anchor(window,host()),x,y,width,height,flags|SWP_NOACTIVATE);
}
inline void maintain(HWND window,HWND desktop){
    if(!IsWindowVisible(window)||!desktop)return;
    // Repair drift in either direction, including changes made with
    // SWP_NOSENDCHANGING that bypass the pre-position guard.
    bool seen=false,repair=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    for(HWND current=GetTopWindow(nullptr);!repair&&current;current=GetWindow(current,GW_HWNDNEXT)){
        if(current==window){seen=true;continue;}
        if(current==desktop){repair=!seen;break;}
        auto owner=GetAncestor(current,GA_ROOTOWNER);
        if(seen&&IsWindowVisible(current)&&!IsIconic(current)&&owner!=desktop&&owner!=GetShellWindow())repair=true;
    }
    if(repair)SetWindowPos(window,anchor(window,desktop),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
}
inline void maintain(HWND window){maintain(window,host());}
}
