#include "lb.h"
static BOOL CALLBACK find_worker(HWND window,LPARAM result) {
    if(FindWindowExW(window,NULL,L"SHELLDLL_DefView",NULL)) {
        HWND worker=FindWindowExW(NULL,window,L"WorkerW",NULL);
        if(worker) { *(HWND*)result=worker; return FALSE; }
    }
    return TRUE;
}
HWND desktop_parent(void) {
    HWND progman=FindWindowW(L"Progman",NULL),worker=NULL;
    if(!progman) return NULL;
    DWORD_PTR ignored;
    SendMessageTimeoutW(progman,0x052c,0xd,0,SMTO_ABORTIFHUNG,700,&ignored);
    SendMessageTimeoutW(progman,0x052c,0xd,1,SMTO_ABORTIFHUNG,700,&ignored);
    EnumWindows(find_worker,(LPARAM)&worker);
    if(!worker) {
        worker=FindWindowExW(progman,NULL,L"WorkerW",NULL);
        if(worker && FindWindowExW(worker,NULL,L"SHELLDLL_DefView",NULL)) worker=NULL;
    }
    return worker;
}
BOOL desktop_attach(HWND window,HMONITOR monitor) {
    HWND parent=desktop_parent(); if(!parent) return FALSE;
    MONITORINFO info={sizeof info}; if(!GetMonitorInfoW(monitor,&info)) return FALSE;
    LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    SetWindowLongPtrW(window,GWL_STYLE,(style&~(WS_POPUP|WS_OVERLAPPEDWINDOW))|WS_CHILD);
    LONG_PTR ext=GetWindowLongPtrW(window,GWL_EXSTYLE);
    SetWindowLongPtrW(window,GWL_EXSTYLE,(ext&~WS_EX_APPWINDOW)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT);
    SetLastError(0); SetParent(window,parent);
    if(GetParent(window)!=parent) return FALSE;
    POINT point={info.rcMonitor.left,info.rcMonitor.top}; MapWindowPoints(NULL,parent,&point,1);
    return SetWindowPos(window,HWND_BOTTOM,point.x,point.y,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
}
BOOL fullscreen_on(HMONITOR monitor,HWND own) {
    HWND fg=GetForegroundWindow();
    if(!fg || fg==own || IsIconic(fg) || !IsWindowVisible(fg)) return FALSE;
    wchar_t name[80];GetClassNameW(fg,name,80);
    if(!wcscmp(name,L"Progman") || !wcscmp(name,L"WorkerW") || !wcsncmp(name,L"Shell_",6)) return FALSE;
    DWORD pid=0;GetWindowThreadProcessId(fg,&pid);if(pid==GetCurrentProcessId()) return FALSE;
    DWORD cloaked=0;DwmGetWindowAttribute(fg,14,&cloaked,sizeof cloaked);if(cloaked) return FALSE;
    RECT r; if(!GetWindowRect(fg,&r)) return FALSE;
    MONITORINFO info={sizeof info}; if(!GetMonitorInfoW(monitor,&info)) return FALSE;
    return r.left<=info.rcMonitor.left+1 && r.top<=info.rcMonitor.top+1 && r.right>=info.rcMonitor.right-1 && r.bottom>=info.rcMonitor.bottom-1;
}
