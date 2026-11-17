#include "lb.h"
#include <stdarg.h>
#include <psapi.h>
#include <stddef.h>

App g_app;
const wchar_t *tr(const wchar_t *zh, const wchar_t *en) { return g_app.english ? en : zh; }

BOOL path_join(wchar_t *dst, size_t cap, const wchar_t *dir, const wchar_t *name) {
    if (!dst || !dir || !name || !cap) return FALSE;
    return SUCCEEDED(StringCchPrintfW(dst, cap, L"%s%s%s", dir,
        *dir && dir[wcslen(dir)-1]!=L'\\' ? L"\\" : L"", name));
}
BOOL ensure_directory(const wchar_t *path) {
    int result=SHCreateDirectoryExW(NULL,path,NULL);
    return result==ERROR_SUCCESS || result==ERROR_ALREADY_EXISTS || result==ERROR_FILE_EXISTS;
}
/* Windows CommandLineToArgvW quoting, including terminal backslashes. */
BOOL quote_arg(wchar_t *dst, size_t cap, const wchar_t *arg) {
    size_t at=0, slashes=0;
    if (!cap || !dst || !arg) return FALSE;
#define PUT(ch) do { if(at+1>=cap) { dst[0]=0; return FALSE; } dst[at++]=(ch); } while(0)
    PUT(L'"');
    for (const wchar_t *p=arg;;++p) {
        if (*p==L'\\') { ++slashes; continue; }
        if (*p==L'"' || !*p) {
            for(size_t n=0;n<slashes*2;++n) PUT(L'\\');
            if(*p) { PUT(L'\\'); PUT(L'"'); }
        } else {
            for(size_t n=0;n<slashes;++n) PUT(L'\\');
            PUT(*p);
        }
        slashes=0;
        if(!*p) break;
    }
    PUT(L'"'); dst[at]=0; return TRUE;
#undef PUT
}
MediaKind media_kind(const wchar_t *path) {
    const wchar_t *e=PathFindExtensionW(path);
    if(!_wcsicmp(e,L".gif")) return MEDIA_GIF;
    if(!_wcsicmp(e,L".jpg") || !_wcsicmp(e,L".jpeg") || !_wcsicmp(e,L".png") || !_wcsicmp(e,L".bmp") || !_wcsicmp(e,L".tif") || !_wcsicmp(e,L".tiff") || !_wcsicmp(e,L".webp")) return MEDIA_IMAGE;
    if(!_wcsicmp(e,L".mp4") || !_wcsicmp(e,L".m4v") || !_wcsicmp(e,L".mov") || !_wcsicmp(e,L".wmv") || !_wcsicmp(e,L".avi") || !_wcsicmp(e,L".webm") || !_wcsicmp(e,L".mkv") || !_wcsicmp(e,L".ogv") || !_wcsicmp(e,L".mpg") || !_wcsicmp(e,L".mpeg") || !_wcsicmp(e,L".ts")) return MEDIA_VIDEO;
    return MEDIA_UNKNOWN;
}
void log_line(const wchar_t *format, ...) {
    wchar_t path[LB_PATH], line[2048], out[2200]; SYSTEMTIME now;
    if(!*g_app.data) return;
    path_join(path,LB_PATH,g_app.data,L"native.log");
    va_list args; va_start(args,format); StringCchVPrintfW(line,2048,format,args); va_end(args);
    GetLocalTime(&now);
    StringCchPrintfW(out,2200,L"%04d-%02d-%02d %02d:%02d:%02d [%lu] %s\r\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),line);
    char utf8[8800]; int bytes=WideCharToMultiByte(CP_UTF8,0,out,-1,utf8,sizeof utf8,NULL,NULL);
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file!=INVALID_HANDLE_VALUE) { DWORD written; WriteFile(file,utf8,(DWORD)(bytes>0?bytes-1:0),&written,NULL); CloseHandle(file); }
}
void set_status(const wchar_t *zh, const wchar_t *en) {
    StringCchCopyW(g_app.status,240,tr(zh,en));
    if(g_app.window) InvalidateRect(g_app.window,NULL,FALSE);
}
void error_box(HWND owner, const wchar_t *action, HRESULT hr) {
    wchar_t message[700];
    StringCchPrintfW(message,700,tr(L"%s\n错误代码：0x%08lX\n详细信息已记录在数据目录中的 native.log。",L"%s\nError: 0x%08lX\nDetails are in native.log in the data folder."),action,(unsigned long)hr);
    log_line(L"%s: 0x%08lX",action,(unsigned long)hr);
    MessageBoxW(owner,message,tr(L"轻块桌面",L"Light Block Desktop"),MB_OK|MB_ICONWARNING);
}
BOOL write_utf8_atomic(const wchar_t *path, const wchar_t *text) {
    wchar_t temp[LB_PATH]; if(FAILED(StringCchPrintfW(temp,LB_PATH,L"%s.tmp",path))) return FALSE;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,NULL,0,NULL,NULL);
    if(bytes<1) return FALSE;
    char *utf8=(char*)malloc((size_t)bytes); if(!utf8) return FALSE;
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,utf8,bytes,NULL,NULL);
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD written=0; BOOL ok=FALSE;
    if(file!=INVALID_HANDLE_VALUE) { ok=WriteFile(file,utf8,(DWORD)bytes-1,&written,NULL) && written==(DWORD)bytes-1; if(ok) ok=FlushFileBuffers(file); CloseHandle(file); }
    free(utf8);
    if(ok) ok=MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) DeleteFileW(temp);
    return ok;
}
wchar_t *read_utf8_file(const wchar_t *path,DWORD limit) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size;if(!GetFileSizeEx(file,&size) || size.QuadPart<0 || size.QuadPart>limit) { CloseHandle(file);return NULL; }
    DWORD bytes=(DWORD)size.QuadPart,read=0;auto *data=static_cast<char*>(calloc((size_t)bytes+1,1));if(!data) { CloseHandle(file);return NULL; }
    BOOL ok=ReadFile(file,data,bytes,&read,NULL)&&read==bytes;CloseHandle(file);if(!ok) { free(data);return NULL; }
    int offset=bytes>=3 && (BYTE)data[0]==0xef && (BYTE)data[1]==0xbb && (BYTE)data[2]==0xbf?3:0;
    int count=bytes>(DWORD)offset?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data+offset,(int)bytes-offset,NULL,0):0;
    if(bytes>(DWORD)offset && !count) { free(data);return NULL; }
    auto *text=static_cast<wchar_t*>(calloc((size_t)count+1,sizeof(wchar_t)));if(text && count) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data+offset,(int)bytes-offset,text,count);free(data);return text;
}
struct SettingField { const wchar_t *section,*key;size_t offset;int initial,low,high; };
#define WIDE_INNER(s) L##s
#define WIDE(s) WIDE_INNER(s)
#define FIELD(section,name,def,lo,hi) {L##section,WIDE(#name),offsetof(Settings,name),def,lo,hi}
static const SettingField fields[]={
 FIELD("organizer",organizer_custom_position,0,0,1),FIELD("organizer",organizer_x,0,-32768,32767),FIELD("organizer",organizer_y,0,-32768,32767),
 FIELD("wallpaper",fit,0,0,3),FIELD("wallpaper",monitor,0,0,16),
 FIELD("wallpaper",fullscreen_pause,1,0,1),FIELD("wallpaper",battery_pause,1,0,1),FIELD("wallpaper",restore_on_start,1,0,1),
 FIELD("organizer",organizer_enabled,0,0,1),FIELD("organizer",organizer_alignment,2,0,2),FIELD("organizer",organizer_view,0,0,2),
 FIELD("organizer",organizer_auto_collect,1,0,1),FIELD("organizer",organizer_icon_size,1,0,2),FIELD("organizer",organizer_opacity,165,30,255),FIELD("organizer",organizer_glass,1,0,1),FIELD("organizer",icon_opacity,100,0,100),
 FIELD("taskbar",taskbar_enabled,0,0,1),FIELD("taskbar",taskbar_radius,22,6,26),FIELD("taskbar",taskbar_opacity,79,0,100),
 FIELD("pointer",cursor_theme,0,0,3),FIELD("pointer",cursor_scale,100,75,200),FIELD("pointer",cursor_particles,0,0,1),FIELD("pointer",particle_style,0,0,2),
 FIELD("capture",capture_enabled,1,0,1),FIELD("capture",capture_modifiers,MOD_CONTROL|MOD_ALT,0,15),FIELD("capture",capture_key,'A',1,254),
 FIELD("capture",record_resolution,0,0,1080),FIELD("capture",record_fps,0,0,60),FIELD("capture",record_lossless,0,0,1),
 FIELD("capture",record_system_audio,0,0,1),FIELD("capture",record_microphone,0,0,1),
 FIELD("tools",clipboard_enabled,0,0,1),FIELD("tools",notes_background,1,0,1),FIELD("tools",hotkeys_enabled,0,0,1),FIELD("wallpaper",wallpaper_sound,0,0,1),
 FIELD("colors",organizer_color,RGB(29,38,49),0,0xffffff),FIELD("colors",icon_color,RGB(66,83,100),0,0xffffff),FIELD("colors",taskbar_color,RGB(38,48,56),0,0xffffff),
 FIELD("colors",cursor_color,RGB(217,242,237),0,0xffffff),FIELD("colors",particle_color,RGB(166,228,202),0,0xffffff)
};
#undef FIELD
void load_config(void) {
    g_app.dark=GetPrivateProfileIntW(L"appearance",L"dark",1,g_app.config)!=0;
    g_app.language=std::clamp(int(GetPrivateProfileIntW(L"appearance",L"language",0,g_app.config)),0,2);
    g_app.english=g_app.language?g_app.language==2:PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_CHINESE;
    g_app.muted=TRUE; /* Each session starts without audio. */
    for(UINT n=0;n<ARRAYSIZE(fields);++n) { const SettingField *f=&fields[n];int value=(int)GetPrivateProfileIntW(f->section,f->key,f->initial,g_app.config);if(value<f->low || value>f->high) value=f->initial;memcpy((BYTE*)&g_app.settings+f->offset,&value,sizeof value); }
    if(!valid_capture_hotkey(UINT(g_app.settings.capture_modifiers),UINT(g_app.settings.capture_key))){g_app.settings.capture_modifiers=MOD_CONTROL|MOD_ALT;g_app.settings.capture_key='A';}
    const wchar_t* views[]={L"view_top",L"view_center",L"view_bottom"};for(int n=0;n<3;++n)g_app.settings.organizer_views[n]=std::clamp(int(GetPrivateProfileIntW(L"organizer",views[n],n==g_app.settings.organizer_alignment?g_app.settings.organizer_view:0,g_app.config)),0,2);g_app.settings.organizer_view=g_app.settings.organizer_views[g_app.settings.organizer_alignment];
    GetPrivateProfileStringW(L"wallpaper",L"active_path",L"",g_app.settings.active_path,LB_PATH,g_app.config);
    GetPrivateProfileStringW(L"capture",L"directory",L"",g_app.settings.capture_directory,LB_PATH,g_app.config);
}
BOOL save_config(void) {
    wchar_t temp[LB_PATH], value[12];
    if(FAILED(StringCchPrintfW(temp,LB_PATH,L"%s.pending",g_app.config))) return FALSE;
    DeleteFileW(temp);
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return FALSE;
    WORD bom=0xfeff;DWORD written;BOOL created=WriteFile(file,&bom,sizeof bom,&written,NULL)&&written==sizeof bom;CloseHandle(file);if(!created) { DeleteFileW(temp);return FALSE; }
    BOOL ok=WritePrivateProfileStringW(L"appearance",L"dark",g_app.dark?L"1":L"0",temp);
    ok=ok && WritePrivateProfileStringW(L"appearance",L"language",g_app.language==0?L"0":g_app.english?L"2":L"1",temp);
    StringCchPrintfW(value,12,L"%u",g_app.filter);
    ok=ok && WritePrivateProfileStringW(L"library",L"filter",value,temp);
    for(UINT n=0;n<ARRAYSIZE(fields) && ok;++n) { int setting;memcpy(&setting,(BYTE*)&g_app.settings+fields[n].offset,sizeof setting);StringCchPrintfW(value,ARRAYSIZE(value),L"%d",setting);ok=WritePrivateProfileStringW(fields[n].section,fields[n].key,value,temp); }
    const wchar_t* views[]={L"view_top",L"view_center",L"view_bottom"};for(int n=0;n<3&&ok;++n){StringCchPrintfW(value,12,L"%d",n==g_app.settings.organizer_alignment?g_app.settings.organizer_view:g_app.settings.organizer_views[n]);ok=WritePrivateProfileStringW(L"organizer",views[n],value,temp);}
    ok=ok && WritePrivateProfileStringW(L"wallpaper",L"active_path",g_app.settings.active_path,temp);
    ok=ok && WritePrivateProfileStringW(L"capture",L"directory",g_app.settings.capture_directory,temp);
    WritePrivateProfileStringW(NULL,NULL,NULL,temp);
    if(ok) ok=MoveFileExW(temp,g_app.config,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok) DeleteFileW(temp);
    return ok;
}
void theme_window(HWND window) {
    BOOL dark=g_app.dark;
    DwmSetWindowAttribute(window,20,&dark,sizeof dark);
    COLORREF bg=dark?RGB(18,23,32):RGB(246,247,250);
    DwmSetWindowAttribute(window,35,&bg,sizeof bg);
    int rounded_corner=2; DwmSetWindowAttribute(window,33,&rounded_corner,sizeof rounded_corner);
}
void draw_text(HDC dc,const wchar_t *text,RECT r,HFONT font,COLORREF color,UINT flags) {
    HFONT old=(HFONT)SelectObject(dc,font); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,text,-1,&r,flags|DT_NOPREFIX); SelectObject(dc,old);
}
void rounded(HDC dc,RECT r,int radius,COLORREF fill,COLORREF border) {
    HBRUSH brush=CreateSolidBrush(fill); HPEN pen=CreatePen(PS_SOLID,1,border);
    HGDIOBJ old_brush=SelectObject(dc,brush), old_pen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,old_brush); SelectObject(dc,old_pen); DeleteObject(brush); DeleteObject(pen);
}
RECT fit_rect(RECT bounds,UINT width,UINT height,BOOL fill) {
    RECT r=bounds; if(!width || !height) return r;
    double x=(double)(bounds.right-bounds.left)/width, y=(double)(bounds.bottom-bounds.top)/height;
    double scale=fill?fmax(x,y):fmin(x,y);
    LONG w=(LONG)lround(width*scale),h=(LONG)lround(height*scale);
    r.left=bounds.left+((bounds.right-bounds.left)-w)/2; r.top=bounds.top+((bounds.bottom-bounds.top)-h)/2;
    r.right=r.left+w; r.bottom=r.top+h; return r;
}
