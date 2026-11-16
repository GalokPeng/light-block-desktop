#include "lb.h"
#include "system_appearance_ui_api.h"
namespace {
std::wstring registry_text(HKEY hive,wchar_t const* path,wchar_t const* name){
    wchar_t text[4096]{};DWORD size=sizeof text,type{};if(RegGetValueW(hive,path,name,RRF_RT_REG_SZ|RRF_RT_REG_MULTI_SZ,&type,text,&size)!=ERROR_SUCCESS)return {};
    size_t count=std::min<size_t>(size/sizeof(wchar_t),ARRAYSIZE(text));for(size_t n=0;n<count;++n)if(!text[n])text[n]=L' ';std::wstring result(text,count);CharLowerBuffW(result.data(),DWORD(result.size()));while(!result.empty()&&iswspace(result.back()))result.pop_back();return result;
}
bool hides_night_light(std::wstring policy){
    CharLowerBuffW(policy.data(),DWORD(policy.size()));auto colon=policy.find(L':');if(colon==std::wstring::npos)return false;
    auto kind=policy.substr(0,colon),pages=L";"+policy.substr(colon+1)+L";";bool listed=pages.find(L";nightlight;")!=std::wstring::npos;
    return (kind==L"hide"&&listed)||(kind==L"showonly"&&!listed);
}
int night_light(){
    using Version=LONG(WINAPI*)(OSVERSIONINFOW*);auto version=reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));OSVERSIONINFOEXW info{};info.dwOSVersionInfoSize=sizeof info;
    if(!version||version(reinterpret_cast<OSVERSIONINFOW*>(&info))||info.dwMajorVersion<10||info.dwBuildNumber<15063||info.wProductType!=VER_NT_WORKSTATION)return LB_NIGHTLIGHT_OS;
    for(HKEY hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})if(hides_night_light(registry_text(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",L"SettingsPageVisibility")))return LB_NIGHTLIGHT_POLICY;
    if(GetSystemMetrics(SM_REMOTESESSION))return LB_NIGHTLIGHT_REMOTE;
    for(DWORD n=0;n<32;++n){DISPLAY_DEVICEW device{};device.cb=sizeof device;if(!EnumDisplayDevicesW(nullptr,n,&device,0))break;if(!(device.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE))continue;
        std::wstring description=std::wstring(device.DeviceString)+L" "+device.DeviceID;std::wstring key=device.DeviceKey,prefix=L"\\Registry\\Machine\\";
        if(key.size()>=prefix.size()&&!_wcsnicmp(key.c_str(),prefix.c_str(),prefix.size()))for(auto value:{L"InstalledDisplayDrivers",L"DriverDesc",L"Service"})description+=L" "+registry_text(HKEY_LOCAL_MACHINE,key.c_str()+prefix.size(),value);
        CharLowerBuffW(description.data(),DWORD(description.size()));for(auto word:{L"basicdisplay",L"basic display",L"displaylink",L"基本显示"})if(description.find(word)!=std::wstring::npos)return LB_NIGHTLIGHT_DRIVER;
    }return LB_NIGHTLIGHT_AVAILABLE;
}
}
SystemAppearanceUiApi const* system_appearance_api(){static SystemAppearanceUiApi const api{LB_APPEARANCE_ABI,sizeof(SystemAppearanceUiApi),night_light};return &api;}
bool system_appearance_tests(){return hides_night_light(L"hide:nightlight;display")&&!hides_night_light(L"hide:display")&&hides_night_light(L"showonly:display")&&!hides_night_light(L"showonly:nightlight;display")&&!hides_night_light(L"")&&!hides_night_light(L"hide:nightlight-extra")&&hides_night_light(L"HIDE:NIGHTLIGHT");}
