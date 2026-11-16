#include "profile.hpp"
#include "migration.hpp"
#include "lb.h"
#include "json.h"
#include "sqlite.hpp"
#include <thread>

namespace lb::profile {
namespace {
namespace fs=std::filesystem;
void require(bool good,char const* error){if(!good)throw std::runtime_error(error);}
std::wstring lower(std::wstring text){if(!text.empty())CharLowerBuffW(text.data(),DWORD(text.size()));return text;}
bool present(fs::path const& path){std::error_code error;bool value=fs::exists(path,error);if(error==std::errc::not_a_directory||error==std::errc::no_such_file_or_directory)return false;if(error)throw fs::filesystem_error("Cannot inspect profile",path,error);return value;}
void no_links(fs::path const& path){for(auto p=fs::absolute(path).lexically_normal();!p.empty();p=p.parent_path()){DWORD flags=GetFileAttributesW(p.c_str());if(flags!=INVALID_FILE_ATTRIBUTES)require(!(flags&FILE_ATTRIBUTE_REPARSE_POINT),"Profile path contains a junction or symbolic link");if(p==p.parent_path())break;}}
bool writable(fs::path const& directory){
    no_links(directory);auto parent=directory;while(!present(parent)){auto next=parent.parent_path();if(next==parent||next.empty())return false;parent=next;}if(!fs::is_directory(parent))return false;
    auto probe=parent/(L".qingkuai-write-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));unique_handle file(CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));return bool(file);
}
fs::path legacy_directory(){PWSTR path=nullptr;HRESULT hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&path);if(SUCCEEDED(hr)&&path){fs::path result=fs::path(path)/LB_PROFILE_DIRECTORY;CoTaskMemFree(path);return result;}if(path)CoTaskMemFree(path);wchar_t buffer[LB_PATH]{};DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,LB_PATH);require(length&&length<LB_PATH,"Cannot locate the per-user data directory");return fs::path(buffer)/LB_PROFILE_DIRECTORY;}
Selection direct(fs::path const& directory){require(writable(directory),"The selected profile directory is not writable");fs::create_directories(directory);Selection out;out.ok=true;out.path=directory;return out;}
std::wstring reg_string(HKEY key,wchar_t const* name){DWORD type{},bytes{};if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes)!=ERROR_SUCCESS||type!=REG_SZ||bytes>65536)return {};std::vector<wchar_t> text(bytes/2+1);if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text.data()),&bytes)!=ERROR_SUCCESS)return {};return text.data();}
bool set_string(HKEY key,wchar_t const* name,std::wstring const& value){return RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<BYTE const*>(value.c_str()),DWORD((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;}
bool inno_record(fs::path const& directory){
    for(auto const& entry:fs::directory_iterator(directory)){
        auto name=lower(entry.path().filename().wstring());if(!name.starts_with(L"unins")||entry.path().extension()!=L".dat"||!entry.is_regular_file())continue;auto uninstaller=entry.path();uninstaller.replace_extension(L".exe");if(!fs::is_regular_file(uninstaller))continue;
        unique_handle file(CreateFileW(entry.path().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)continue;char data[8192]{};DWORD read{};if(!ReadFile(file.get(),data,sizeof data,&read,nullptr))continue;std::string_view header(data,read);if(header.starts_with("Inno Setup Uninstall Log")&&header.find(sql::utf8(LB_INSTALL_APP_ID))!=std::string_view::npos)return true;
    }return false;
}
struct PreparationWindow {
    HWND window{};HFONT font{};bool english{PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_CHINESE};
    ~PreparationWindow(){if(window)DestroyWindow(window);if(font)DeleteObject(font);}
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto self=reinterpret_cast<PreparationWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<PreparationWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(self));}
        if(message==WM_CLOSE)return 0;
        if(message==WM_CTLCOLORSTATIC){SetTextColor(HDC(wp),RGB(231,238,247));SetBkColor(HDC(wp),RGB(25,32,42));SetDCBrushColor(HDC(wp),RGB(25,32,42));return LRESULT(GetStockObject(DC_BRUSH));}
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(window,&r);SetDCBrushColor(HDC(wp),RGB(25,32,42));FillRect(HDC(wp),&r,HBRUSH(GetStockObject(DC_BRUSH)));return 1;}
        if(message==WM_CREATE&&self){self->font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");auto label=CreateWindowExW(0,L"STATIC",self->english?L"Preparing your settings and data…\r\nPreview settings are kept separate.":L"正在准备设置与数据，请稍候…\r\n测试版设置独立保存。",WS_CHILD|WS_VISIBLE,24,18,450,55,window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(label,WM_SETFONT,WPARAM(self->font),TRUE);auto bar=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_MARQUEE,24,88,450,9,window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(bar,PBM_SETMARQUEE,TRUE,35);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    void show(){
        INITCOMMONCONTROLSEX controls{sizeof controls,ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LightBlockDesktop.ProfilePreparation.v1";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&cls);RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,english?L"Light Block Desktop":L"轻块桌面",WS_OVERLAPPED|WS_CAPTION,(area.left+area.right-500)/2,(area.top+area.bottom-150)/2,500,150,nullptr,nullptr,cls.hInstance,this);if(window){BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);}
    }
};
}
Selection select(fs::path const& executable,fs::path const& custom,fs::path const& override_legacy) noexcept {
    try {
        auto exe=fs::absolute(executable).lexically_normal();auto primary=exe.parent_path()/L"data-preview";
        auto fallback=override_legacy.empty()?legacy_directory():fs::absolute(override_legacy).lexically_normal();
        auto chosen=custom.empty()?(writable(primary)?primary:fallback):fs::absolute(custom).lexically_normal();no_links(chosen);
        require(!present(chosen/L"settings.json"),"This is a Python stable profile. Preview builds require an independent data directory; no files were changed.");
        return direct(chosen);
    }catch(std::exception const& e){Selection out;try{out.error=sql::wide(e.what());}catch(...){out.error=L"Could not select an independent preview profile.";}return out;}catch(...){Selection out;out.error=L"Could not select an independent preview profile.";return out;}
}
Selection select_with_progress(fs::path const& executable,fs::path const& custom) noexcept {
    try{
        unique_handle complete(CreateEventW(nullptr,TRUE,FALSE,nullptr));require(bool(complete),"Cannot create profile preparation event");Selection result;
        std::thread worker([&]{result=select(executable,custom);SetEvent(complete.get());});scope_exit join([&]{if(worker.joinable())worker.join();});PreparationWindow progress;auto started=GetTickCount64();bool shown=false;bool quitting=false;int quit_code=0;
        for(;;){DWORD remaining=shown?INFINITE:DWORD(GetTickCount64()-started>=400?0:400-(GetTickCount64()-started));HANDLE event=complete.get();DWORD ready=MsgWaitForMultipleObjects(1,&event,FALSE,remaining,QS_ALLINPUT);if(ready==WAIT_OBJECT_0)break;if(ready==WAIT_FAILED){WaitForSingleObject(event,INFINITE);break;}MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){quitting=true;quit_code=int(message.wParam);continue;}TranslateMessage(&message);DispatchMessageW(&message);}if(!shown&&GetTickCount64()-started>=400&&WaitForSingleObject(event,0)!=WAIT_OBJECT_0){progress.show();shown=true;}}
        worker.join();if(quitting)PostQuitMessage(quit_code);return result;
    }catch(std::exception const& e){Selection out;try{out.error=sql::wide(e.what());}catch(...){out.error=L"Could not prepare the application data.";}return out;}catch(...){Selection out;out.error=L"Could not prepare the application data.";return out;}
}
std::vector<MenuEntry> desktop_menu_entries(fs::path const& executable,bool english,fs::path const& profile,bool muted){
    auto exe=fs::absolute(executable).lexically_normal();require(lower(exe.filename().wstring())==lower(LB_PRODUCT_EXE),"Desktop integration requires the official executable name");wchar_t quoted[LB_PATH*2]{};require(quote_arg(quoted,ARRAYSIZE(quoted),exe.c_str()),"Executable path is too long");std::wstring base=std::wstring(L"Software\\Classes\\DesktopBackground\\Shell\\")+LB_DESKTOP_MENU_PREFIX+L".";std::wstring icon=std::wstring(quoted)+L",0";
    std::wstring options;if(!profile.empty()){wchar_t data[LB_PATH*2]{};require(quote_arg(data,ARRAYSIZE(data),profile.c_str()),"Profile path is too long");options=L" --data-dir "+std::wstring(data);}
    auto brand=english?std::wstring(LB_PRODUCT_ENGLISH):std::wstring(LB_PRODUCT_NAME);
    return {{base+L"note",brand+(english?L" · New note":L" · 新建便签"),icon,std::wstring(quoted)+options+L" --desktop-action note"},{base+L"sound",brand+(english?(muted?L" · Enable wallpaper sound":L" · Mute wallpaper sound"):(muted?L" · 开启壁纸声音":L" · 关闭壁纸声音")),icon,std::wstring(quoted)+options+L" --desktop-action sound"}};

}
bool is_installed(fs::path const& executable) noexcept {
    try{
        auto exe=fs::weakly_canonical(executable);if(lower(exe.filename().wstring())!=lower(LB_PRODUCT_EXE)||!fs::is_regular_file(exe))return false;bool registered=false;
        auto uninstall=std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\")+LB_INSTALL_APP_ID+L"_is1";
        for(HKEY hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){HKEY key{};if(RegOpenKeyExW(hive,uninstall.c_str(),0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS)continue;auto path=reg_string(key,L"InstallLocation");if(path.empty())path=reg_string(key,L"Inno Setup: App Path");RegCloseKey(key);if(path.empty()||!fs::path(path).is_absolute())continue;registered=true;if(lower(fs::weakly_canonical(path).wstring())==lower(exe.parent_path().wstring()))return true;}
        return !registered&&inno_record(exe.parent_path());
    }catch(...){return false;}
}
bool desktop_menu(fs::path const& executable,bool enabled,bool official,bool english,fs::path const& profile,bool muted) noexcept {
    if(!official)return true;
    try {
        auto entries=desktop_menu_entries(executable,english,profile,muted);
        // Preview keys are disjoint from the stable product. Never overwrite a
        // foreign command even if it was manually placed under the preview key.
        for(auto const& entry:entries){HKEY command{};if(RegOpenKeyExW(HKEY_CURRENT_USER,(entry.key+L"\\command").c_str(),0,KEY_QUERY_VALUE,&command)==ERROR_SUCCESS){auto existing=reg_string(command,L"");RegCloseKey(command);if(!existing.empty()){int argc=0;auto argv=CommandLineToArgvW(existing.c_str(),&argc);bool owned=argv&&argc>0&&lower(fs::path(argv[0]).filename().wstring())==lower(LB_PRODUCT_EXE);if(argv)LocalFree(argv);require(owned,"Desktop menu is owned by a different application");}}}
        bool ok=true;
        for(auto const& entry:entries){
            if(!enabled){LONG result=RegDeleteTreeW(HKEY_CURRENT_USER,entry.key.c_str());ok=ok&&(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND);continue;}
            HKEY key{},command{};if(RegCreateKeyExW(HKEY_CURRENT_USER,entry.key.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
            bool changed=set_string(key,L"MUIVerb",entry.label)&&set_string(key,L"Icon",entry.icon);if(changed&&RegCreateKeyExW(key,L"command",0,nullptr,0,KEY_SET_VALUE,nullptr,&command,nullptr)==ERROR_SUCCESS){changed=set_string(command,L"",entry.command);RegCloseKey(command);}else changed=false;RegCloseKey(key);ok=ok&&changed;
        }
        return ok;
    }catch(...){return false;}
}
bool tests(fs::path const& root){
    try{
        require(!present(root),"Profile fixture directory must be new");fs::create_directories(root/L"install");auto exe=root/L"install"/LB_PRODUCT_EXE,fallback=root/L"preview-fallback";
        auto fresh=select(exe,{},fallback);require(fresh.ok&&fresh.path==exe.parent_path()/L"data-preview"&&!fresh.migrated,"Preview profile was not independent");
        auto stable=exe.parent_path()/L"data";fs::create_directories(stable);require(write_utf8_atomic((stable/L"settings.json").c_str(),L"{\"version\":\"1.0.0\",\"library\":[]}"),"Fixture old profile failed");auto again=select(exe,{},fallback);require(again.ok&&again.path==fresh.path&&!again.migrated&&!present(stable/L"profile-v1.1"),"Preview touched a stable profile");
        auto refused=select(exe,stable,fallback);require(!refused.ok&&present(stable/L"settings.json"),"Explicit stable profile must be refused without mutation");
        auto custom=select(exe,root/L"custom",fallback);require(custom.ok&&custom.path==root/L"custom","Explicit preview profile changed");
        auto denied=root/L"denied";fs::create_directories(denied);require(write_utf8_atomic((denied/L"data-preview").c_str(),L"blocking file"),"Fixture blocker failed");auto other=select(denied/LB_PRODUCT_EXE,{},fallback);require(other.ok&&other.path==fallback,"Preview-only fallback failed");
        auto plan=desktop_menu_entries(exe,false,root/L"custom",true);require(plan.size()==2&&plan[0].key.find(LB_DESKTOP_MENU_PREFIX)!=std::wstring::npos&&plan[0].command.find(L" --desktop-action note")!=std::wstring::npos&&plan[1].command.find(L" --desktop-action sound")!=std::wstring::npos&&plan[0].command.find(L" --data-dir ")!=std::wstring::npos&&plan[0].icon.ends_with(L",0"),"Preview desktop command plan invalid");require(desktop_menu(exe,true,false),"Fixture registry guard failed");
        auto records=root/L"inno-records";fs::create_directories(records);auto identity=std::wstring(L"Inno Setup Uninstall Log\n")+LB_INSTALL_APP_ID;require(write_utf8_atomic((records/L"unins000.dat").c_str(),identity.c_str()),"Inno fixture failed");require(!inno_record(records),"Uninstaller without executable trusted");require(write_utf8_atomic((records/L"unins000.exe").c_str(),L"fixture placeholder")&&inno_record(records),"Preview identity rejected");
        return write_utf8_atomic((root/L"result.json").c_str(),L"{\"preview_isolated\":true,\"stable_untouched\":true,\"explicit_stable_rejected\":true,\"custom_exact\":true,\"preview_fallback\":true,\"desktop_profile_command\":true,\"registry_untouched\":true,\"preview_install_identity\":true}")!=FALSE;
    }catch(std::exception const& e){std::fprintf(stderr,"Profile fixture failed: %s\n",e.what());return false;}catch(...){return false;}
}
}
