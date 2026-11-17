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
inline void place(HWND window,int x,int y,int width,int height,UINT flags){
    SetWindowPos(window,anchor(window,host()),x,y,width,height,flags|SWP_NOACTIVATE);
}
inline void maintain(HWND window){
    if(!IsWindowVisible(window))return;HWND desktop=host();if(!desktop)return;
    for(HWND current=GetTopWindow(nullptr);current;current=GetWindow(current,GW_HWNDNEXT)){
        if(current==window)return;
        if(current==desktop){place(window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);return;}
    }
}
}
