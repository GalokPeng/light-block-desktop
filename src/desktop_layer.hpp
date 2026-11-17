#pragma once
#include "lb.h"
namespace lb::desktop_layer {
inline HWND host(){
    HWND result{};EnumWindows([](HWND window,LPARAM parameter)->BOOL{
        if(FindWindowExW(window,nullptr,L"SHELLDLL_DefView",nullptr)){*reinterpret_cast<HWND*>(parameter)=window;return FALSE;}return TRUE;
    },LPARAM(&result));return result?result:GetShellWindow();
}
// HWND_BOTTOM can be *below* Explorer's WorkerW. Place desktop tools directly
// above the icon host, while retaining normal application windows above them.
inline void place(HWND window,int x,int y,int width,int height,UINT flags){
    HWND desktop=host(),after=desktop?GetWindow(desktop,GW_HWNDPREV):HWND_BOTTOM;
    if(after==window)flags|=SWP_NOZORDER;
    SetWindowPos(window,after?after:HWND_TOP,x,y,width,height,flags|SWP_NOACTIVATE);
}
inline void maintain(HWND window){
    if(!IsWindowVisible(window))return;HWND desktop=host();if(!desktop)return;
    for(HWND current=GetTopWindow(nullptr);current;current=GetWindow(current,GW_HWNDNEXT)){
        if(current==window)return;
        if(current==desktop){place(window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);return;}
    }
}
}
