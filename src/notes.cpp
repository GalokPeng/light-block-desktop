#include "lb.h"
#include "frontend.hpp"
#include <commdlg.h>

struct Note { HWND window,edit; COLORREF color; wchar_t file[LB_PATH]; HBRUSH brush; BOOL dirty,loading; };
static Note notes[32];static BOOL shutting_down;
static int note_meta_get(const wchar_t *file,const wchar_t *key,int initial) { wchar_t path[LB_PATH];StringCchPrintfW(path,LB_PATH,L"%s.ini",file);return (int)GetPrivateProfileIntW(L"note",key,initial,path); }
static void note_meta_set(const wchar_t *file,const wchar_t *key,int value) { wchar_t path[LB_PATH],text[32];StringCchPrintfW(path,LB_PATH,L"%s.ini",file);StringCchPrintfW(text,32,L"%d",value);WritePrivateProfileStringW(L"note",key,text,path); }
static COLORREF contrast(COLORREF c) { return GetRValue(c)*299+GetGValue(c)*587+GetBValue(c)*114>140000?RGB(30,39,44):RGB(245,247,251); }
static BOOL note_save(Note *n) {
    if(n->loading) return TRUE;
    int length=GetWindowTextLengthW(n->edit);wchar_t *text=(wchar_t*)calloc((size_t)length+1,sizeof(wchar_t));if(!text) return FALSE;
    GetWindowTextW(n->edit,text,length+1);BOOL ok=write_utf8_atomic(n->file,text);free(text);
    if(ok) { n->dirty=FALSE;note_meta_set(n->file,L"color",(int)n->color);RECT r;GetWindowRect(n->window,&r);note_meta_set(n->file,L"x",r.left);note_meta_set(n->file,L"y",r.top);note_meta_set(n->file,L"width",r.right-r.left);note_meta_set(n->file,L"height",r.bottom-r.top); }
    return ok;
}
static LRESULT CALLBACK note_edit(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    (void)id;(void)data;
    if(message==WM_MOUSEWHEEL) { SendMessageW(window,EM_LINESCROLL,0,-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3);return 0; }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,note_edit,1);
    return DefSubclassProc(window,message,wp,lp);
}
static void note_color(Note *n) {
    static COLORREF custom[16];CHOOSECOLORW choose={sizeof choose};choose.hwndOwner=n->window;choose.rgbResult=n->color;choose.lpCustColors=custom;choose.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorW(&choose)) { n->color=choose.rgbResult;DeleteObject(n->brush);n->brush=CreateSolidBrush(n->color);InvalidateRect(n->window,NULL,TRUE);InvalidateRect(n->edit,NULL,TRUE);n->dirty=TRUE;SetTimer(n->window,1,650,NULL); }
}
static LRESULT CALLBACK note_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    Note *n=(Note*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) { n=(Note*)((CREATESTRUCTW*)lp)->lpCreateParams;n->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)n); }
    if(!n) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:
        n->brush=CreateSolidBrush(n->color);
        n->loading=TRUE;n->edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,18,48,300,200,window,(HMENU)1,g_app.instance,NULL);
        SendMessageW(n->edit,WM_SETFONT,(WPARAM)g_app.font,TRUE);SendMessageW(n->edit,EM_SETLIMITTEXT,128*1024,0);SetWindowSubclass(n->edit,note_edit,1,0);return 0;
    case WM_SIZE:MoveWindow(n->edit,18,48,LB_MAX(50,LOWORD(lp)-36),LB_MAX(40,HIWORD(lp)-66),TRUE);if(!n->loading) { n->dirty=TRUE;SetTimer(window,1,650,NULL); }return 0;
    case WM_EXITSIZEMOVE:n->dirty=TRUE;SetTimer(window,1,650,NULL);return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize=POINT{230,160};return 0;
    case WM_NCHITTEST: {
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);RECT r;GetClientRect(window,&r);
        BOOL l=p.x<7,rr=p.x>r.right-7,t=p.y<7,b=p.y>r.bottom-7;
        if(t&&l) return HTTOPLEFT;if(t&&rr) return HTTOPRIGHT;if(b&&l) return HTBOTTOMLEFT;if(b&&rr) return HTBOTTOMRIGHT;
        if(l) return HTLEFT;if(rr) return HTRIGHT;if(t) return HTTOP;if(b) return HTBOTTOM;
        if(p.y<40 && p.x<r.right-72) return HTCAPTION;return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT r;GetClientRect(window,&r);FillRect(dc,&r,n->brush);
        draw_text(dc,tr(L"便签",L"Note"),RECT{18,10,r.right-85,36},g_app.small,contrast(n->color),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        draw_text(dc,L"◉",RECT{r.right-72,8,r.right-40,36},g_app.font,contrast(n->color),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        draw_text(dc,L"×",RECT{r.right-38,8,r.right-8,36},g_app.heading,contrast(n->color),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(window,&ps);return 0;
    }
    case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,contrast(n->color));SetBkColor((HDC)wp,n->color);return (LRESULT)n->brush;
    case WM_COMMAND:if(HIWORD(wp)==EN_CHANGE && !n->loading) { n->dirty=TRUE;SetTimer(window,1,650,NULL); }return 0;
    case WM_TIMER:KillTimer(window,1);if(n->dirty) note_save(n);return 0;
    case WM_LBUTTONUP: {
        RECT r;GetClientRect(window,&r);if(GET_Y_LPARAM(lp)<40) { if(GET_X_LPARAM(lp)>r.right-40) SendMessageW(window,WM_CLOSE,0,0);else if(GET_X_LPARAM(lp)>r.right-76) note_color(n); }return 0;
    }
    case WM_CONTEXTMENU: {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,tr(L"调色板…",L"Choose color…"));AppendMenuW(menu,MF_STRING,2,tr(L"切换完成状态",L"Toggle completed"));AppendMenuW(menu,MF_STRING,3,tr(L"归档",L"Archive"));AppendMenuW(menu,MF_STRING,4,tr(L"管理便签与提醒…",L"Manage notes and reminders…"));POINT p;GetCursorPos(&p);int id=TrackPopupMenu(menu,TPM_RETURNCMD,p.x,p.y,0,window,NULL);DestroyMenu(menu);
        if(id==1) note_color(n);else if(id==2) { note_meta_set(n->file,L"done",!note_meta_get(n->file,L"done",0));notes_tick(); }else if(id==3) { note_meta_set(n->file,L"archived",1);SendMessageW(window,WM_CLOSE,0,0); }else if(id==4) notes_manager();return 0;
    }
    case WM_CLOSE:
        if(n->dirty && !note_save(n)) { error_box(window,tr(L"便签保存失败，请保留窗口后重试",L"Could not save this note. Keep it open and retry."),HRESULT_FROM_WIN32(GetLastError()));return 0; }
        if(!shutting_down) note_meta_set(n->file,L"pinned",0);DestroyWindow(window);return 0;
    case WM_DESTROY:DeleteObject(n->brush);ZeroMemory(n,sizeof *n);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void note_open_saved(const wchar_t *file,BOOL activate) {
    if(lb::frontend::note_open(file,activate!=FALSE)) return;
    Note *n=NULL;for(UINT i=0;i<ARRAYSIZE(notes);++i) { if(notes[i].window && !_wcsicmp(notes[i].file,file)) { ShowWindow(notes[i].window,SW_SHOW);if(activate) SetForegroundWindow(notes[i].window);return; }if(!notes[i].window && !n) n=&notes[i]; }
    if(!n) return;
    WNDCLASSW cls={};cls.lpfnWndProc=note_proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Note";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);RegisterClassW(&cls);
    StringCchCopyW(n->file,LB_PATH,file);n->color=(COLORREF)note_meta_get(file,L"color",(int)RGB(255,234,176));
    int x=note_meta_get(file,L"x",180),y=note_meta_get(file,L"y",160),width=LB_MAX(230,LB_MIN(1800,note_meta_get(file,L"width",370))),height=LB_MAX(160,LB_MIN(1400,note_meta_get(file,L"height",300)));
    RECT rect={x,y,x+width,y+height};MONITORINFO info={sizeof info};GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&info);x=LB_MAX(info.rcWork.left,LB_MIN(x,info.rcWork.right-100));y=LB_MAX(info.rcWork.top,LB_MIN(y,info.rcWork.bottom-80));
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"LightBlockDesktopNative.Note",L"轻块桌面 · 便签",WS_POPUP|WS_THICKFRAME,x,y,width,height,GetShellWindow(),NULL,g_app.instance,n);
    if(window) { wchar_t *text=read_utf8_file(file,1024*1024);if(!text) { log_line(L"note cannot be decoded; leaving file unchanged");DestroyWindow(window);return; }SetWindowTextW(n->edit,text);free(text);n->loading=FALSE;n->dirty=FALSE;note_meta_set(file,L"pinned",1);theme_window(window);ShowWindow(window,activate?SW_SHOW:SW_SHOWNOACTIVATE);if(activate) SetFocus(n->edit); }
}
void note_create(HWND owner) {
    (void)owner;wchar_t directory[LB_PATH],file[LB_PATH],name[80];path_join(directory,LB_PATH,g_app.data,L"notes");if(!ensure_directory(directory)) return;
    FILETIME ft;GetSystemTimeAsFileTime(&ft);StringCchPrintfW(name,80,L"note-%08lx%08lx.txt",ft.dwHighDateTime,ft.dwLowDateTime);path_join(file,LB_PATH,directory,name);
    if(write_utf8_atomic(file,L"")) note_open_saved(file,TRUE);else error_box(g_app.window,tr(L"无法创建便签",L"Could not create a note"),E_FAIL);
}
void note_close_saved(const wchar_t *file) { if(lb::frontend::note_close(file)) return;for(UINT n=0;n<ARRAYSIZE(notes);++n) if(notes[n].window && !wcscmp(notes[n].file,file)) SendMessageW(notes[n].window,WM_CLOSE,0,0); }
BOOL notes_shutdown(void) { if(!lb::frontend::notes_shutdown(true)) return FALSE;shutting_down=TRUE;KillTimer(g_app.window,14);for(UINT i=0;i<ARRAYSIZE(notes);++i) if(notes[i].window) { SendMessageW(notes[i].window,WM_CLOSE,0,0);if(notes[i].window) { shutting_down=FALSE;return FALSE; } }return TRUE; }
void notes_restore(void) {
    shutting_down=FALSE;wchar_t directory[LB_PATH],pattern[LB_PATH],file[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"notes");path_join(pattern,LB_PATH,directory,L"note-*.txt");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE) return;
    if(g_app.settings.notes_background) do { path_join(file,LB_PATH,directory,data.cFileName);if(note_meta_get(file,L"pinned",0) && !note_meta_get(file,L"archived",0)) note_open_saved(file,FALSE); }while(FindNextFileW(find,&data));FindClose(find);notes_tick();
}
