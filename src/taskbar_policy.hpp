#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lb::taskbar {
inline bool work_area_frame(RECT frame,bool maximized,RECT before,RECT after){
    if(before.left!=after.left||before.top!=after.top||before.right!=after.right||before.bottom==after.bottom)return false;
    constexpr int tolerance=3;
    if(std::abs(frame.bottom-before.bottom)>tolerance||frame.left<before.left-tolerance||frame.right>before.right+tolerance||frame.top<before.top-tolerance)return false;
    // Chromium/WeChat document windows can fill rcWork without WS_MAXIMIZE.
    return maximized||(std::abs(frame.left-before.left)<=tolerance&&std::abs(frame.right-before.right)<=tolerance&&std::abs(frame.top-before.top)<=tolerance);
}
inline bool write_work_area(RECT area){
    if(!SystemParametersInfoW(SPI_SETWORKAREA,0,&area,0))return false;
    // Broadcasting back to Explorer makes it immediately reserve the strip
    // again on Windows 11. Notify application consumers, excluding that shell.
    DWORD shell=0;GetWindowThreadProcessId(GetShellWindow(),&shell);
    EnumWindows([](HWND window,LPARAM owner)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid&&pid!=DWORD(owner))SendNotifyMessageW(window,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
        return TRUE;
    },LPARAM(shell));return true;
}
inline bool bottom_edge(POINT p,RECT monitor){return p.x>=monitor.left&&p.x<monitor.right&&p.y>=monitor.bottom-2&&p.y<monitor.bottom;}
inline bool recent_edge(MOUSEMOVEPOINT const* points,int count,DWORD now,RECT monitor){
    for(int n=0;n<count;++n){auto p=points[n];if(DWORD(now-p.time)>350)continue;
        if(p.x>32767)p.x-=65536;if(p.y>32767)p.y-=65536;
        if(bottom_edge({p.x,p.y},monitor))return true;
    }return false;
}
inline bool recent_edge(POINT cursor,RECT monitor){
    MOUSEMOVEPOINT input{cursor.x&0xffff,cursor.y&0xffff,0,0},points[64]{};
    int count=GetMouseMovePointsEx(sizeof(input),&input,points,64,GMMP_USE_DISPLAY_POINTS);
    return count>0&&recent_edge(points,count,GetTickCount(),monitor);
}
}
