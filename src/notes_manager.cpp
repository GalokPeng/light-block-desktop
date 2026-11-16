#include "lb.h"
#include "frontend.hpp"

struct Row { wchar_t file[LB_PATH],title[150];BOOL pinned,archived,done;ULONGLONG due; };
static struct { HWND window,list,search,archive;Row *rows;UINT count,capacity;HBRUSH brush; } manager;
static struct { HWND window,date,time;wchar_t file[LB_PATH]; } reminder;
static ULONGLONG utc_now(void) { FILETIME f;GetSystemTimeAsFileTime(&f);return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime; }
static void meta_path(wchar_t *path,const wchar_t *file) { StringCchPrintfW(path,LB_PATH,L"%s.ini",file); }
static int meta_int(const wchar_t *file,const wchar_t *key,int initial) { wchar_t path[LB_PATH];meta_path(path,file);return (int)GetPrivateProfileIntW(L"note",key,initial,path); }
static void meta_set(const wchar_t *file,const wchar_t *key,int value) { wchar_t path[LB_PATH],number[32];meta_path(path,file);StringCchPrintfW(number,32,L"%d",value);WritePrivateProfileStringW(L"note",key,number,path); }
static ULONGLONG due_get(const wchar_t *file) { wchar_t path[LB_PATH],value[40];meta_path(path,file);GetPrivateProfileStringW(L"note",L"due",L"0",value,40,path);return _wcstoui64(value,NULL,10); }
static void due_set(const wchar_t *file,ULONGLONG due) { wchar_t path[LB_PATH],value[40];meta_path(path,file);StringCchPrintfW(value,40,L"%llu",due);WritePrivateProfileStringW(L"note",L"due",value,path);meta_set(file,L"notified",0);notes_tick(); }
static void scan(void) {
    manager.count=0;wchar_t directory[LB_PATH],pattern[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"notes");path_join(pattern,LB_PATH,directory,L"note-*.txt");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE) return;
    do { if(manager.count>=512) break;if(manager.count==manager.capacity) { UINT capacity=manager.capacity?manager.capacity*2:16;auto *rows=static_cast<Row*>(realloc(manager.rows,(size_t)capacity*sizeof(Row)));if(!rows) break;manager.rows=rows;manager.capacity=capacity; }Row *row=&manager.rows[manager.count];ZeroMemory(row,sizeof *row);path_join(row->file,LB_PATH,directory,data.cFileName);row->pinned=meta_int(row->file,L"pinned",0);row->archived=meta_int(row->file,L"archived",0);row->done=meta_int(row->file,L"done",0);row->due=due_get(row->file);
        wchar_t *text=read_utf8_file(row->file,1024*1024);if(text) { StringCchCopyNW(row->title,150,text,140);wchar_t *newline=wcspbrk(row->title,L"\r\n");if(newline) *newline=0;free(text); }if(!*row->title) StringCchCopyW(row->title,150,tr(L"未命名便签",L"Untitled note"));++manager.count;
    }while(FindNextFileW(find,&data));FindClose(find);
}
static void due_label(ULONGLONG due,wchar_t *label,size_t cap) {
    if(!due) { *label=0;return; }FILETIME utc={(DWORD)due,(DWORD)(due>>32)},local;SYSTEMTIME time;FileTimeToLocalFileTime(&utc,&local);FileTimeToSystemTime(&local,&time);StringCchPrintfW(label,cap,L"%04u-%02u-%02u %02u:%02u",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute);
}
static void refresh(void) {
    if(!manager.window) return;scan();wchar_t query[260];GetWindowTextW(manager.search,query,260);BOOL archived=SendMessageW(manager.archive,BM_GETCHECK,0,0)==BST_CHECKED;
    SendMessageW(manager.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(manager.list);
    for(UINT n=0;n<manager.count;++n) {
        Row *row=&manager.rows[n];if(row->archived!=archived) continue;
        if(*query && !StrStrIW(row->title,query)) { wchar_t *body=read_utf8_file(row->file,1024*1024);BOOL found=body && StrStrIW(body,query);free(body);if(!found) continue; }
        LVITEMW item={};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=ListView_GetItemCount(manager.list);item.pszText=row->title;item.lParam=n;int index=ListView_InsertItem(manager.list,&item);
        ListView_SetItemText(manager.list,index,1,(wchar_t*)(row->done?tr(L"已完成",L"Done"):tr(L"待办",L"Open")));ListView_SetItemText(manager.list,index,2,(wchar_t*)(row->pinned?tr(L"桌面",L"Pinned"):L""));wchar_t due[80];due_label(row->due,due,80);ListView_SetItemText(manager.list,index,3,due);
    }SendMessageW(manager.list,WM_SETREDRAW,TRUE,0);InvalidateRect(manager.list,NULL,TRUE);
}
static Row *selected(void) { int index=ListView_GetNextItem(manager.list,-1,LVNI_SELECTED);if(index<0) return NULL;LVITEMW item={};item.mask=LVIF_PARAM;item.iItem=index;if(!ListView_GetItem(manager.list,&item) || (UINT)item.lParam>=manager.count) return NULL;return &manager.rows[item.lParam]; }
static LRESULT CALLBACK reminder_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_CREATE) { reminder.date=CreateWindowExW(0,DATETIMEPICK_CLASSW,L"",WS_CHILD|WS_VISIBLE|DTS_SHORTDATECENTURYFORMAT,22,28,190,30,window,(HMENU)1,g_app.instance,NULL);reminder.time=CreateWindowExW(0,DATETIMEPICK_CLASSW,L"",WS_CHILD|WS_VISIBLE|DTS_TIMEFORMAT,224,28,130,30,window,(HMENU)2,g_app.instance,NULL);CreateWindowExW(0,L"BUTTON",tr(L"保存提醒",L"Save reminder"),WS_CHILD|WS_VISIBLE,140,85,140,34,window,(HMENU)3,g_app.instance,NULL);return 0; }
    if(message==WM_COMMAND && LOWORD(wp)==3) { SYSTEMTIME date,time;DateTime_GetSystemtime(reminder.date,&date);DateTime_GetSystemtime(reminder.time,&time);date.wHour=time.wHour;date.wMinute=time.wMinute;date.wSecond=0;date.wMilliseconds=0;FILETIME local,utc;if(SystemTimeToFileTime(&date,&local) && LocalFileTimeToFileTime(&local,&utc)) { due_set(reminder.file,((ULONGLONG)utc.dwHighDateTime<<32)|utc.dwLowDateTime);DestroyWindow(window);refresh(); }return 0; }
    if(message==WM_CLOSE) { DestroyWindow(window);return 0; }if(message==WM_DESTROY) reminder.window=NULL;return DefWindowProcW(window,message,wp,lp);
}
static LRESULT CALLBACK manager_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        manager.window=window;manager.brush=CreateSolidBrush(g_app.dark?RGB(26,33,45):RGB(246,247,250));
        manager.search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,22,20,440,32,window,(HMENU)1,g_app.instance,NULL);SendMessageW(manager.search,EM_SETCUEBANNER,TRUE,(LPARAM)tr(L"搜索便签全文…",L"Search note text…"));
        manager.archive=CreateWindowExW(0,L"BUTTON",tr(L"已归档",L"Archived"),WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,490,22,140,30,window,(HMENU)2,g_app.instance,NULL);
        manager.list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL,22,72,775,360,window,(HMENU)3,g_app.instance,NULL);ListView_SetExtendedListViewStyle(manager.list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t *zh[]={L"便签内容",L"状态",L"位置",L"到期提醒"},*en[]={L"Note",L"Status",L"Location",L"Reminder"};int widths[]={350,85,85,210};for(int n=0;n<4;++n) { LVCOLUMNW col={};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=widths[n];col.pszText=(wchar_t*)tr(zh[n],en[n]);ListView_InsertColumn(manager.list,n,&col); }
        const wchar_t *buttons_zh[]={L"新建",L"打开／贴到桌面",L"完成／未完成",L"归档／恢复",L"设置提醒",L"清除提醒"},*buttons_en[]={L"New",L"Open / pin",L"Toggle done",L"Archive / restore",L"Set reminder",L"Clear reminder"};
        for(int n=0;n<6;++n) { HWND button=CreateWindowExW(0,L"BUTTON",tr(buttons_zh[n],buttons_en[n]),WS_CHILD|WS_VISIBLE,22+n*130,454,122,36,window,(HMENU)(uintptr_t)(10+n),g_app.instance,NULL);SendMessageW(button,WM_SETFONT,(WPARAM)g_app.font,0); }
        SendMessageW(manager.list,WM_SETFONT,(WPARAM)g_app.font,0);SendMessageW(manager.search,WM_SETFONT,(WPARAM)g_app.font,0);SendMessageW(manager.archive,WM_SETFONT,(WPARAM)g_app.font,0);ListView_SetBkColor(manager.list,g_app.dark?RGB(26,33,45):RGB(255,255,255));ListView_SetTextBkColor(manager.list,CLR_NONE);ListView_SetTextColor(manager.list,g_app.dark?RGB(235,240,249):RGB(29,38,49));refresh();return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==1 && HIWORD(wp)==EN_CHANGE) SetTimer(window,1,250,NULL);
        else if(LOWORD(wp)==2) refresh();else if(LOWORD(wp)==10) { note_create(window);refresh(); }
        else { Row *row=selected();if(!row) return 0;int id=LOWORD(wp);
            if(id==11) { note_open_saved(row->file,TRUE);refresh(); }
            else if(id==12) { meta_set(row->file,L"done",!row->done);refresh();notes_tick(); }
            else if(id==13) { BOOL archive=!row->archived;wchar_t file[LB_PATH];StringCchCopyW(file,LB_PATH,row->file);meta_set(file,L"archived",archive);if(archive) note_close_saved(file);refresh();notes_tick(); }
            else if(id==14) { if(reminder.window) DestroyWindow(reminder.window);StringCchCopyW(reminder.file,LB_PATH,row->file);WNDCLASSW cls={};cls.lpfnWndProc=reminder_proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Reminder";cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);reminder.window=CreateWindowExW(0,cls.lpszClassName,tr(L"到期提醒",L"Reminder"),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,280,230,400,180,window,NULL,g_app.instance,NULL);ShowWindow(reminder.window,SW_SHOW); }
            else if(id==15) { due_set(row->file,0);refresh(); }
        }return 0;
    case WM_TIMER:KillTimer(window,1);refresh();return 0;
    case WM_NOTIFY:if(((NMHDR*)lp)->hwndFrom==manager.list && ((NMHDR*)lp)->code==NM_DBLCLK) { Row *row=selected();if(row) note_open_saved(row->file,TRUE); }return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,g_app.dark?RGB(235,240,249):RGB(29,38,49));return (LRESULT)manager.brush;
    case WM_ERASEBKGND: { RECT r;GetClientRect(window,&r);FillRect((HDC)wp,&r,manager.brush);return 1; }
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:if(reminder.window) DestroyWindow(reminder.window);DeleteObject(manager.brush);free(manager.rows);manager.rows=NULL;manager.count=manager.capacity=0;manager.window=NULL;manager.list=NULL;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void notes_manager(void) {
    if(lb::frontend::active()) { lb::frontend::notes_manager();return; }
    if(manager.window) { refresh();ShowWindow(manager.window,SW_RESTORE);SetForegroundWindow(manager.window);return; }
    WNDCLASSW cls={};cls.lpfnWndProc=manager_proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Notes";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,tr(L"便签与任务",L"Notes and tasks"),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,180,120,840,560,g_app.window,NULL,g_app.instance,NULL);if(window) { theme_window(window);ShowWindow(window,SW_SHOW); }
}
void notes_tick(void) {
    KillTimer(g_app.window,14);
    /* A temporary index keeps open manager row identifiers stable during reminders. */
    ULONGLONG now=utc_now(),next=0;wchar_t directory[LB_PATH],pattern[LB_PATH],file[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"notes");path_join(pattern,LB_PATH,directory,L"note-*.txt");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE) return;
    do { path_join(file,LB_PATH,directory,data.cFileName);ULONGLONG due=due_get(file);if(!due || meta_int(file,L"done",0) || meta_int(file,L"archived",0) || meta_int(file,L"notified",0)) continue;
        if(due<=now) { NOTIFYICONDATAW notice=g_app.tray;notice.uFlags=NIF_INFO;notice.dwInfoFlags=NIIF_INFO;StringCchCopyW(notice.szInfoTitle,64,tr(L"便签到期提醒",L"Note reminder"));wchar_t *text=read_utf8_file(file,1024*1024);StringCchCopyNW(notice.szInfo,256,text?text:tr(L"便签已到期",L"A note is due"),250);free(text);Shell_NotifyIconW(NIM_MODIFY,&notice);meta_set(file,L"notified",1); }
        else if(!next || due<next) next=due;
    }while(FindNextFileW(find,&data));FindClose(find);if(next) SetTimer(g_app.window,14,(UINT)LB_MAX(1000,LB_MIN(0x7fffffffu,(next-now)/10000)),NULL);
}
