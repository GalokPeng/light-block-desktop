#include "lb.h"
#include "version.hpp"
#include "taskbar.hpp"
#include "organizer.hpp"
#include "pointer.hpp"
#include "clipboard.hpp"
#include "particles.hpp"
#include "recording.hpp"
#include "ui_api.h"
#include <commdlg.h>
#include <stddef.h>

typedef enum { CHECK,COMBO,SLIDER,COLOR,ACTION,HOTKEY } FieldKind;
struct Field { int page;const wchar_t *zh,*en;FieldKind kind;size_t offset;int low,high;const wchar_t *choices_zh,*choices_en; };
#define F(page,name,zh,en,kind,lo,hi,cz,ce) {page,L##zh,L##en,kind,offsetof(Settings,name),lo,hi,L##cz,L##ce}
static const Field options[]={
 F(0,fit,"壁纸适应方式","Wallpaper fit",COMBO,0,3,"填充屏幕|完整显示|拉伸铺满|原始大小","Fill|Fit|Stretch|Original size"),
 F(0,monitor,"显示器","Monitor",COMBO,0,16,"主显示器","Primary display"),
 F(0,fullscreen_pause,"前台全屏时暂停动态壁纸","Pause on fullscreen",CHECK,0,1,"",""),
 F(0,battery_pause,"使用电池时暂停动态壁纸","Pause on battery",CHECK,0,1,"",""),
 F(0,restore_on_start,"启动时恢复上次的壁纸","Restore this app's wallpaper on startup",CHECK,0,1,"",""),
 F(1,organizer_enabled,"启用桌面收纳","Enable desktop organizer",CHECK,0,1,"",""),
 F(1,organizer_alignment,"布局位置","Shelf position",COMBO,0,2,"居上|居中|居下","Top|Center|Bottom"),
 F(1,organizer_view,"排列方式","Sort by",COMBO,0,2,"名称|修改时间|分组","Name|Modified time|Groups"),
 F(1,organizer_icon_size,"图标大小 · 小 / 中 / 大","Icon size · Small / Medium / Large",SLIDER,0,2,"",""),
 F(1,organizer_auto_collect,"一分钟后自动收纳新增内容","Collect new items after one minute",CHECK,0,1,"",""),
 F(1,organizer_glass,"磨砂玻璃背景","Frosted background",CHECK,0,1,"",""),
 F(1,organizer_opacity,"背景不透明度","Background opacity",SLIDER,30,255,"",""),
 F(1,organizer_color,"收纳背景颜色","Organizer background",COLOR,0,0xffffff,"",""),
 F(1,icon_color,"图标底板颜色","Icon background",COLOR,0,0xffffff,"",""),
 F(2,taskbar_enabled,"启用原生任务栏美化","Style the native taskbar",CHECK,0,1,"",""),
 F(2,taskbar_radius,"圆角","Corner radius",SLIDER,6,26,"",""),
 F(2,taskbar_opacity,"背景不透明度","Background opacity",SLIDER,0,100,"",""),
 F(2,taskbar_color,"任务栏颜色","Taskbar color",COLOR,0,0xffffff,"",""),
 F(3,cursor_theme,"鼠标指针风格","Cursor style",COMBO,0,3,"系统默认|流线|棱镜|像素","System|Stream|Outline|Pixel"),
 F(3,cursor_scale,"指针大小 (%)","Cursor size (%)",SLIDER,75,200,"",""),
 F(3,cursor_color,"指针颜色","Cursor color",COLOR,0,0xffffff,"",""),
 F(3,cursor_particles,"启用鼠标粒子","Enable pointer particles",CHECK,0,1,"",""),
 F(3,particle_style,"粒子风格","Particle style",COMBO,0,2,"点点|飘落|光芒","Dots|Fall|Contour glow"),
 F(3,particle_color,"粒子颜色","Particle color",COLOR,0,0xffffff,"",""),
 F(4,capture_enabled,"启用截屏快捷键","Enable capture hotkey",CHECK,0,1,"",""),
 F(4,capture_key,"截屏快捷键","Capture shortcut",HOTKEY,0,254,"",""),
 F(4,record_resolution,"导出画质","Export resolution",COMBO,0,3,"原始分辨率|1080p|720p|480p","Original|1080p|720p|480p"),
 F(4,record_fps,"导出帧率","Export frame rate",COMBO,0,5,"原始帧率|12 fps|15 fps|24 fps|30 fps|60 fps","Original|12 fps|15 fps|24 fps|30 fps|60 fps"),
 F(4,record_lossless,"无损 MKV 录屏源","Lossless MKV source",CHECK,0,1,"",""),
 F(5,notes_background,"启动时恢复已贴桌面的便签","Restore pinned notes on startup",CHECK,0,1,"",""),
 F(5,clipboard_enabled,"记录本地剪贴板历史（默认关闭）","Record local clipboard history (opt-in)",CHECK,0,1,"",""),
 F(5,hotkeys_enabled,"其他全局快捷键 · Ctrl+Alt+R / Space / V","Other shortcuts · Ctrl+Alt+R / Space / V",CHECK,0,1,"",""),
 F(0,wallpaper_sound,"视频壁纸声音","Video wallpaper sound",CHECK,0,1,"",""),
 F(4,record_system_audio,"录制系统声音","Record system audio",CHECK,0,1,"",""),
 F(4,record_microphone,"录制麦克风","Record microphone",CHECK,0,1,"","")
};
#undef F
static struct { HWND window,viewport,sections[6],controls[ARRAYSIZE(options)],labels[ARRAYSIZE(options)],startup,night,save,start,status;HBRUSH brush;int page,family,scroll,content;bool expanded[6],startup_checked,startup_initial;UINT hotkey_id;Settings draft,baseline; } s_options;
static int get_value(const Field *f) { int value;memcpy(&value,(BYTE*)&s_options.draft+f->offset,sizeof value);return value; }
static void set_value(const Field *f,int value) { memcpy((BYTE*)&s_options.draft+f->offset,&value,sizeof value); }
static const int resolutions[]={0,1080,720,480},rates[]={0,12,15,24,30,60};
static void choices(HWND combo,const wchar_t *values) {
    wchar_t buffer[600];StringCchCopyW(buffer,600,values);wchar_t *context=NULL,*word=wcstok_s(buffer,L"|",&context);
    while(word) { SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)word);word=wcstok_s(NULL,L"|",&context); }
}
struct DisplayChoice {int value{},ordinal{},width{},height{},scale{100};bool primary{};std::wstring name;};
static std::wstring display_name(wchar_t const* device) {
    UINT32 path_count{},mode_count{};if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&path_count,&mode_count)==ERROR_SUCCESS&&path_count<=64&&mode_count<=256){
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&path_count,paths.data(),&mode_count,modes.data(),nullptr)==ERROR_SUCCESS)for(UINT32 n=0;n<path_count;++n){
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof source,paths[n].sourceInfo.adapterId,paths[n].sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||_wcsicmp(source.viewGdiDeviceName,device))continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof target,paths[n].targetInfo.adapterId,paths[n].targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS&&target.monitorFriendlyDeviceName[0])return target.monitorFriendlyDeviceName;
        }
    }
    DISPLAY_DEVICEW output{};output.cb=sizeof output;if(EnumDisplayDevicesW(device,0,&output,0)&&output.DeviceString[0])return output.DeviceString;return {};
}
static std::wstring display_label(DisplayChoice const& item,bool english,size_t maximum=74){
    auto name=item.name;std::replace(name.begin(),name.end(),L'|',L' ');if(name.find(L"\\\\.\\")!=std::wstring::npos)name.clear();
    auto prefix=std::wstring(english?L"Display ":L"显示器 ")+std::to_wstring(item.ordinal)+(item.primary?(english?L" (Primary)":L"（主屏）"):L"");
    auto detail=L" · "+std::to_wstring(item.width)+L"×"+std::to_wstring(item.height)+L" · "+std::to_wstring(item.scale)+L"%";
    size_t room=maximum>prefix.size()+detail.size()+3?maximum-prefix.size()-detail.size()-3:0;
    if(!name.empty()&&room>3){if(name.size()>room)name=name.substr(0,room-1)+L"…";prefix+=L" · "+name;}return prefix+detail;
}
static std::vector<DisplayChoice> const& displays(){
    static std::vector<DisplayChoice> cached;static ULONGLONG checked{};auto now=GetTickCount64();if(!cached.empty()&&now-checked<2000)return cached;checked=now;cached.clear();
    auto callback=+[](HMONITOR monitor,HDC,LPRECT,LPARAM context)->BOOL {
        auto& items=*reinterpret_cast<std::vector<DisplayChoice>*>(context);MONITORINFOEXW info{};info.cbSize=sizeof info;if(!GetMonitorInfoW(monitor,&info))return TRUE;
        DisplayChoice item;item.ordinal=int(items.size())+1;item.value=item.ordinal;item.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;item.width=info.rcMonitor.right-info.rcMonitor.left;item.height=info.rcMonitor.bottom-info.rcMonitor.top;item.name=display_name(info.szDevice);
        using Scale=HRESULT(WINAPI*)(HMONITOR,int*);static HMODULE module=LoadLibraryExW(L"shcore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);static auto scale=module?reinterpret_cast<Scale>(GetProcAddress(module,"GetScaleFactorForMonitor")):nullptr;
        int percent=100;if(scale&&SUCCEEDED(scale(monitor,&percent))&&percent>=100&&percent<=500)item.scale=percent;items.push_back(std::move(item));return items.size()<16;
    };EnumDisplayMonitors(nullptr,nullptr,callback,LPARAM(&cached));return cached;
}
static void describe_displays(UiSetting& out){
    auto const& items=displays();out.choices[0]=0;out.choice_count=0;size_t budget=items.empty()?799:799/items.size()-1;
    auto append=[&](DisplayChoice const& item){auto text=display_label(item,g_app.english,budget);if(out.choice_count)StringCchCatW(out.choices,800,L"|");StringCchCatW(out.choices,800,text.c_str());out.choice_values[out.choice_count++]=item.primary?0:item.value;if(item.primary&&out.value==item.value)out.value=0;};
    for(auto const& item:items)if(item.primary)append(item);for(auto const& item:items)if(!item.primary)append(item);
    if(!out.choice_count){StringCchCopyW(out.choices,800,tr(L"主显示器（暂时无法读取详情）",L"Primary display (details unavailable)"));out.choice_values[0]=0;out.choice_count=1;out.value=0;}
}
static void populate_display_combo(HWND combo,int value){UiSetting option{};option.value=value;describe_displays(option);SendMessageW(combo,CB_RESETCONTENT,0,0);choices(combo,option.choices);int selected=0;for(int n=0;n<option.choice_count;++n){SendMessageW(combo,CB_SETITEMDATA,n,option.choice_values[n]);if(option.choice_values[n]==option.value)selected=n;}SendMessageW(combo,CB_SETCURSEL,selected,0);}
bool settings_display_tests(){
    DisplayChoice item{2,2,3840,2160,150,true,L"DELL U2723QE"};auto zh=display_label(item,false),en=display_label(item,true);
    if(zh.find(L"DELL U2723QE")==std::wstring::npos||zh.find(L"3840×2160")==std::wstring::npos||en.find(L"150%")==std::wstring::npos||en.find(L"Primary")==std::wstring::npos)return false;
    item.name=L"\\\\.\\DISPLAY1";if(display_label(item,false).find(L"DISPLAY")!=std::wstring::npos)return false;
    item.name=std::wstring(100,L'名');return display_label(item,false,48).size()<=48;
}
BOOL startup_enabled(void) {
    HKEY key;DWORD type=0,size=0;BOOL exists=FALSE;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS) { exists=RegQueryValueExW(key,LB_AUTORUN_KEY,NULL,&type,NULL,&size)==ERROR_SUCCESS;RegCloseKey(key); }return exists;
}
BOOL startup_set(BOOL enabled) {
    HKEY key;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);if(result!=ERROR_SUCCESS) return FALSE;
    if(enabled) { wchar_t exe[LB_PATH*2],data[LB_PATH*2],command[LB_PATH*4+100];quote_arg(exe,ARRAYSIZE(exe),g_app.exe);quote_arg(data,ARRAYSIZE(data),g_app.data);StringCchPrintfW(command,ARRAYSIZE(command),L"%s --background --data-dir %s",exe,data);result=RegSetValueExW(key,LB_AUTORUN_KEY,0,REG_SZ,(BYTE*)command,(DWORD)((wcslen(command)+1)*2)); }
    else { result=RegDeleteValueW(key,LB_AUTORUN_KEY);if(result==ERROR_FILE_NOT_FOUND) result=ERROR_SUCCESS; }
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
BOOL night_light_supported(void) {
    typedef LONG (WINAPI *VersionFn)(OSVERSIONINFOW*);VersionFn version=(VersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");OSVERSIONINFOW info={sizeof info};return version && version(&info)==0 && info.dwBuildNumber>=15063;
}
bool valid_capture_hotkey(UINT modifiers,UINT key){
    if(modifiers>15||key<1||key>254||key==VK_F12||key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LWIN||key==VK_RWIN)return false;
    if(!(modifiers&(MOD_CONTROL|MOD_ALT|MOD_WIN))&&!((key>=VK_F1&&key<=VK_F24)||key==VK_SNAPSHOT))return false;
    return modifiers!=(MOD_CONTROL|MOD_ALT)||(key!='R'&&key!='V'&&key!=VK_SPACE);
}
static bool tool_registered[3]{};
bool tool_hotkey_registered(int id){return id>=11&&id<=13&&tool_registered[id-11];}
template<class Register,class Unregister> static BOOL update_tool_hotkeys(bool (&registered)[3],bool enabled,bool recording,Register add,Unregister remove){
    bool desired[]={enabled||recording,enabled,enabled},added[3]{};UINT keys[]={'R',VK_SPACE,'V'};
    for(int n=0;n<3;++n)if(desired[n]&&!registered[n]){
        if(!add(11+n,keys[n])){DWORD error=GetLastError();for(int k=0;k<3;++k)if(added[k])remove(11+k);SetLastError(error);return FALSE;}added[n]=true;
    }
    for(int n=0;n<3;++n){if(!desired[n]&&registered[n])remove(11+n);registered[n]=desired[n];}return TRUE;
}
BOOL configure_tool_hotkeys(bool enabled,bool recording){
    return update_tool_hotkeys(tool_registered,enabled,recording,[](int id,UINT key){return RegisterHotKey(g_app.window,id,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,key);},[](int id){UnregisterHotKey(g_app.window,id);});
}
static void apply_wallpaper_audio(){
    auto &wallpaper=g_app.wallpaper;bool muted=g_app.settings.wallpaper_sound==0;
    if(wallpaper.ready&&wallpaper.window&&media_kind(wallpaper.pending_path)==MEDIA_VIDEO&&bool(wallpaper.muted)!=muted){wallpaper.muted=muted;PostMessageW(wallpaper.window,WM_LB_CONTROL,2,0);}
    if(&player_active()==&wallpaper)g_app.muted=wallpaper.muted;
}
BOOL configure_hotkey(UINT modifiers,UINT key) {
    if(!g_app.settings.capture_enabled) { UnregisterHotKey(g_app.window,1);UnregisterHotKey(g_app.window,2);s_options.hotkey_id=0;return TRUE; }
    if(!valid_capture_hotkey(modifiers,key)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    UINT id=s_options.hotkey_id==1?2:1;
    if(s_options.hotkey_id && g_app.settings.capture_modifiers==(int)modifiers && g_app.settings.capture_key==(int)key) return TRUE;
    if(!RegisterHotKey(g_app.window,(int)id,modifiers|MOD_NOREPEAT,key)) return FALSE;
    if(s_options.hotkey_id) UnregisterHotKey(g_app.window,(int)s_options.hotkey_id);s_options.hotkey_id=id;
    g_app.settings.capture_modifiers=(int)modifiers;g_app.settings.capture_key=(int)key;return TRUE;
}
int settings_change(int id,int value);
static BOOL apply(void) {
    // The fallback native pane uses the same delta path as the Qt frontend.
    // Untouched controls never overwrite newer tray / asynchronous service state.
    for(UINT n=0;n<ARRAYSIZE(options);++n) {
        const Field *f=&options[n];HWND control=s_options.controls[n];int value=get_value(f),baseline{};
        memcpy(&baseline,reinterpret_cast<BYTE*>(&s_options.baseline)+f->offset,sizeof baseline);
        if(f->kind==CHECK) value=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(f->kind==SLIDER) value=int(SendMessageW(control,TBM_GETPOS,0,0));
        else if(f->kind==COMBO) { value=int(SendMessageW(control,CB_GETCURSEL,0,0));if(f->offset==offsetof(Settings,monitor))value=int(SendMessageW(control,CB_GETITEMDATA,value,0));if(f->offset==offsetof(Settings,record_resolution)) value=resolutions[std::clamp(value,0,3)];if(f->offset==offsetof(Settings,record_fps)) value=rates[std::clamp(value,0,5)]; }
        else if(f->kind==HOTKEY) { WORD key=WORD(SendMessageW(control,HKM_GETHOTKEY,0,0));BYTE flags=HIBYTE(key);int mods=((flags&HOTKEYF_CONTROL)?MOD_CONTROL:0)|((flags&HOTKEYF_ALT)?MOD_ALT:0)|((flags&HOTKEYF_SHIFT)?MOD_SHIFT:0);value=LOBYTE(key)|(mods<<16);baseline|=s_options.baseline.capture_modifiers<<16; }
        if(value==baseline) continue;
        if(!settings_change(int(n),value)) { SetWindowTextW(s_options.status,tr(L"此项未能应用，已保留原设置。",L"This change could not be applied; the previous value was retained."));return FALSE; }
        int actual=value;if(f->kind==HOTKEY) { actual=value&0xffff;s_options.baseline.capture_modifiers=(value>>16)&15; }
        memcpy(reinterpret_cast<BYTE*>(&s_options.baseline)+f->offset,&actual,sizeof actual);set_value(f,actual);
    }
    if(s_options.startup_checked!=s_options.startup_initial) { if(!settings_change(int(ARRAYSIZE(options)),s_options.startup_checked)) return FALSE;s_options.startup_initial=s_options.startup_checked; }
    set_status(L"设置已保存",L"Settings saved.");return TRUE;
}
#include "settings_view.hpp"
int settings_describe(int id,UiSetting* out) {
    if(!out) return 0;*out={};out->id=id;
    if(id==int(ARRAYSIZE(options))) { out->section=0;out->kind=CHECK;out->value=startup_enabled();StringCchCopyW(out->label,160,tr(L"开机启动",L"Start with Windows"));return 1; }
    if(id<0||id>=int(ARRAYSIZE(options))) return 0;auto const& field=options[id];out->section=field.page;out->kind=field.kind;out->low=field.low;out->high=field.high;memcpy(&out->value,reinterpret_cast<BYTE*>(&g_app.settings)+field.offset,sizeof(int));StringCchCopyW(out->label,160,tr(field.zh,field.en));StringCchCopyW(out->choices,800,tr(field.choices_zh,field.choices_en));
    if(field.kind==COMBO) {
        for(auto p=out->choices;*p;++p) if(*p==L'|') ++out->choice_count;if(*out->choices) ++out->choice_count;
        if(field.offset==offsetof(Settings,monitor)) {describe_displays(*out);return 1;}
        for(int n=0;n<out->choice_count&&n<24;++n) out->choice_values[n]=field.offset==offsetof(Settings,record_resolution)?resolutions[n]:field.offset==offsetof(Settings,record_fps)?rates[n]:n;
    }
    if(field.kind==HOTKEY) out->value|=g_app.settings.capture_modifiers<<16;return 1;
}
int settings_change(int id,int value) {
    if(id==int(ARRAYSIZE(options))) { bool ok=startup_set(value!=0);if(!ok) set_status(L"开机启动未能更改",L"Startup could not be changed.");return ok; }
    if(id<0||id>=int(ARRAYSIZE(options))) return 0;auto const& field=options[id];Settings before=g_app.settings;Settings next=before;
    if(field.kind==COMBO) { UiSetting description{};settings_describe(id,&description);if(std::find(description.choice_values,description.choice_values+description.choice_count,value)==description.choice_values+description.choice_count) return 0; }
    else if(field.kind!=HOTKEY&&(value<field.low||value>field.high)) return 0;
    if(field.kind==HOTKEY) { next.capture_key=value&0xffff;next.capture_modifiers=(value>>16)&15;if(!valid_capture_hotkey(UINT(next.capture_modifiers),UINT(next.capture_key))){set_status(L"请使用 Ctrl、Alt 或 Win 组合键；F12 和工具快捷键保留。",L"Use Ctrl, Alt or Win; F12 and tool shortcuts are reserved.");return 0;} }
    else memcpy(reinterpret_cast<BYTE*>(&next)+field.offset,&value,sizeof(int));
    if(field.offset==offsetof(Settings,organizer_alignment)){next.organizer_custom_position=0;next.organizer_views[before.organizer_alignment]=before.organizer_view;next.organizer_view=next.organizer_views[next.organizer_alignment];}
    if(field.offset==offsetof(Settings,organizer_view))next.organizer_views[next.organizer_alignment]=value;
    if(!memcmp(&before,&next,sizeof next)) return 1;
    bool hotkey=next.capture_enabled!=before.capture_enabled||next.capture_key!=before.capture_key||next.capture_modifiers!=before.capture_modifiers;
    auto busy=[](wchar_t const* name) { lb::unique_handle resource(CreateMutexW(nullptr,FALSE,name));return !resource||GetLastError()==ERROR_ALREADY_EXISTS; };
    if((!before.taskbar_enabled&&next.taskbar_enabled&&!lb::taskbar::has_session()&&busy(L"Local\\Still.TaskbarStyle"))||(!before.organizer_enabled&&next.organizer_enabled&&!lb::organizer::has_session()&&busy(L"Local\\Still.DesktopIconVisibility"))||(!before.cursor_theme&&next.cursor_theme&&!lb::pointer::has_session()&&busy(L"Local\\Qingkuai.PointerStyle"))) { set_status(L"另一个轻块实例正在使用这项美化，请先在该实例中关闭对应功能。",L"Another Light Block instance owns this feature. Disable it there first.");return 0; }
    if(field.offset==offsetof(Settings,wallpaper_sound)&&value&&!before.wallpaper_sound){auto owner=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(owner,&pid);if(pid!=GetCurrentProcessId())owner=nullptr;if(MessageBoxW(owner,tr(L"开启声音会启用音频设备，部分视频或驱动可能出现卡顿。仍要启用视频壁纸声音吗？",L"Audio opens an audio device and may cause stuttering with some videos or drivers. Enable wallpaper sound?"),tr(LB_PRODUCT_NAME,LB_PRODUCT_ENGLISH),MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION|MB_SETFOREGROUND)!=IDYES)return 0;}
    if(field.offset==offsetof(Settings,hotkeys_enabled)&&!configure_tool_hotkeys(value!=0,lb::recording::active())){set_status(L"部分工具快捷键已被其他程序占用，已保留原设置。",L"Some tool shortcuts are in use. The previous setting was retained.");return 0;}
    g_app.settings=next;
    if(hotkey) { g_app.settings.capture_key=before.capture_key;g_app.settings.capture_modifiers=before.capture_modifiers;if(!configure_hotkey(UINT(next.capture_modifiers),UINT(next.capture_key))) { g_app.settings=before;set_status(L"快捷键被占用，已保留原设置",L"Shortcut unavailable; previous shortcut retained.");return 0; }g_app.settings=next; }
    if(!save_config()) { g_app.settings=before;if(field.offset==offsetof(Settings,hotkeys_enabled))configure_tool_hotkeys(before.hotkeys_enabled!=0,lb::recording::active());if(hotkey) { UnregisterHotKey(g_app.window,1);UnregisterHotKey(g_app.window,2);s_options.hotkey_id=0;configure_hotkey(UINT(before.capture_modifiers),UINT(before.capture_key)); }set_status(L"设置保存失败，已保留原设置",L"Save failed; previous setting retained.");return 0; }
    if(field.offset==offsetof(Settings,wallpaper_sound)){apply_wallpaper_audio();PostMessageW(g_app.window,WM_COMMAND,507,0);}
    // Commit one field. Never copy a stale page snapshot over asynchronous module state.
    if(field.page==1) { if(!lb::organizer::configure()&&g_app.settings.organizer_enabled) { g_app.settings.organizer_enabled=0;save_config();set_status(L"桌面收纳未能启动，原桌面保持不变",L"Organizer could not start. Your original desktop is unchanged.");return 0; } }
    else if(field.page==2) lb::taskbar::configure();
    else if(field.page==3) {
        if(field.offset==offsetof(Settings,cursor_theme)||field.offset==offsetof(Settings,cursor_color)||field.offset==offsetof(Settings,cursor_scale)) { if(!lb::pointer::configure()&&g_app.settings.cursor_theme) { g_app.settings.cursor_theme=0;save_config();set_status(L"指针美化未能启动，已保留系统指针",L"Pointer styling could not start. System cursors were retained.");return 0; } }
        else if(!lb::particles::configure()) { g_app.settings.cursor_particles=0;save_config();set_status(L"鼠标特效未能启动",L"Pointer effect could not start.");return 0; }
    }else if(field.offset==offsetof(Settings,clipboard_enabled)) { if(!lb::clipboard::configure()) { g_app.settings.clipboard_enabled=0;save_config();set_status(L"剪贴板记录启动失败",L"Clipboard recording could not start.");return 0; } }
    else if(field.offset==offsetof(Settings,monitor)) player_settings_changed();
    else if(field.offset==offsetof(Settings,fit)||field.offset==offsetof(Settings,fullscreen_pause)||field.offset==offsetof(Settings,battery_pause)) player_refresh_settings();
    return 1;
}
bool settings_patch_test() {
    auto before=g_app.settings;auto draft=s_options.draft;wchar_t config[LB_PATH];StringCchCopyW(config,LB_PATH,g_app.config);lb::scope_exit restore([&]{g_app.settings=before;s_options.draft=draft;StringCchCopyW(g_app.config,LB_PATH,config);save_config();});
    int fps=-1;for(UINT n=0;n<ARRAYSIZE(options);++n) if(options[n].offset==offsetof(Settings,record_fps)) fps=int(n);if(fps<0) return false;
    s_options.draft=before;g_app.settings.taskbar_enabled=1;g_app.settings.organizer_enabled=1;StringCchCopyW(g_app.settings.active_path,LB_PATH,L"C:\\independent-fixture\\active.mp4");
    if(!settings_change(fps,24)||!g_app.settings.taskbar_enabled||!g_app.settings.organizer_enabled||wcscmp(g_app.settings.active_path,L"C:\\independent-fixture\\active.mp4")) return false;
    load_config();if(g_app.settings.record_fps!=24||!g_app.settings.taskbar_enabled||!g_app.settings.organizer_enabled) return false;
    if(settings_change(fps,999)||g_app.settings.record_fps!=24) return false;
    for(auto offset:{offsetof(Settings,record_system_audio),offsetof(Settings,record_microphone)}){
        int id=-1;for(UINT n=0;n<ARRAYSIZE(options);++n)if(options[n].offset==offset)id=int(n);if(id<0)return false;
        if(!settings_recording_audio(offset==offsetof(Settings,record_microphone),true))return false;load_config();int value{};memcpy(&value,reinterpret_cast<BYTE*>(&g_app.settings)+offset,sizeof value);if(value!=1||settings_change(id,2)||!settings_recording_audio(offset==offsetof(Settings,record_microphone),false))return false;
    }
    StringCchCopyW(g_app.config,LB_PATH,(std::filesystem::path(g_app.data)/L"missing-settings-folder"/L"config.ini").c_str());if(settings_change(fps,30)||g_app.settings.record_fps!=24||settings_recording_audio(false,true)||settings_recording_audio(true,true)||g_app.settings.record_system_audio||g_app.settings.record_microphone) return false;return true;
}

int settings_wallpaper_sound(bool enabled){for(UINT n=0;n<ARRAYSIZE(options);++n)if(options[n].offset==offsetof(Settings,wallpaper_sound))return settings_change(int(n),enabled?1:0);return 0;}
int settings_recording_audio(bool microphone,bool enabled){auto offset=microphone?offsetof(Settings,record_microphone):offsetof(Settings,record_system_audio);for(UINT n=0;n<ARRAYSIZE(options);++n)if(options[n].offset==offset)return settings_change(int(n),enabled?1:0);return 0;}
bool settings_runtime_tests(){
    // Exercise registration conflicts without reserving any real global shortcut.
    bool registered[3]{},actual[3]{};int fail=13;
    auto add=[&](int id,UINT key)->BOOL{if(id==fail){SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);return FALSE;}if((id==11&&key!='R')||(id==12&&key!=VK_SPACE)||(id==13&&key!='V'))return FALSE;actual[id-11]=true;return TRUE;};
    auto remove=[&](int id){actual[id-11]=false;};
    if(update_tool_hotkeys(registered,true,false,add,remove)||GetLastError()!=ERROR_HOTKEY_ALREADY_REGISTERED||actual[0]||actual[1]||actual[2]||registered[0])return false;
    if(!update_tool_hotkeys(registered,false,true,add,remove)||!actual[0]||actual[1])return false;
    if(update_tool_hotkeys(registered,true,true,add,remove)||!actual[0]||actual[1]||actual[2]||!registered[0])return false;
    fail=0;if(!update_tool_hotkeys(registered,true,true,add,remove)||!actual[0]||!actual[1]||!actual[2])return false;
    if(!update_tool_hotkeys(registered,false,true,add,remove)||!actual[0]||actual[1]||actual[2])return false;
    if(!update_tool_hotkeys(registered,false,false,add,remove)||actual[0]||actual[1]||actual[2])return false;
    auto before=g_app.settings;auto muted=g_app.muted;Player wallpaper=std::move(g_app.wallpaper),preview=std::move(g_app.player);
    lb::scope_exit restore([&]{g_app.settings=before;g_app.muted=muted;g_app.wallpaper=std::move(wallpaper);g_app.player=std::move(preview);save_config();});
    HWND sink=CreateWindowExW(0,L"STATIC",L"audio-fixture",0,0,0,1,1,HWND_MESSAGE,nullptr,g_app.instance,nullptr);if(!sink)return false;lb::scope_exit close([&]{DestroyWindow(sink);});
    g_app.wallpaper={};g_app.player={};g_app.wallpaper.window=sink;g_app.wallpaper.ready=TRUE;g_app.wallpaper.muted=TRUE;g_app.player.muted=FALSE;
    StringCchCopyW(g_app.wallpaper.pending_path,LB_PATH,L"fixture.mp4");g_app.settings.wallpaper_sound=1;apply_wallpaper_audio();
    MSG message{};if(g_app.wallpaper.muted||g_app.player.muted||!PeekMessageW(&message,sink,WM_LB_CONTROL,WM_LB_CONTROL,PM_REMOVE)||message.wParam!=2)return false;
    if(!save_config())return false;g_app.settings.wallpaper_sound=0;load_config();if(!g_app.settings.wallpaper_sound)return false;
    if(!settings_wallpaper_sound(false)||!g_app.wallpaper.muted||g_app.player.muted)return false;load_config();if(g_app.settings.wallpaper_sound)return false;
    // Stills and GIFs must not open an audio device, even with the preference on.
    while(PeekMessageW(&message,sink,WM_LB_CONTROL,WM_LB_CONTROL,PM_REMOVE)){}
    StringCchCopyW(g_app.wallpaper.pending_path,LB_PATH,L"fixture.gif");g_app.settings.wallpaper_sound=1;apply_wallpaper_audio();
    return g_app.wallpaper.muted&&!PeekMessageW(&message,sink,WM_LB_CONTROL,WM_LB_CONTROL,PM_REMOVE);
}
