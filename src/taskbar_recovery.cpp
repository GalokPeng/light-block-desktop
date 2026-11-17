#include "taskbar_recovery.hpp"
#include "taskbar_policy.hpp"
#include "json.h"
#include <functional>

namespace lb::taskbar {
namespace {
struct Record {
    uint64_t window{},shell_created{},owner_created{};
    DWORD shell{},owner{};
    bool work{},region{},topmost{},region_present{};
    RECT before{},after{},bounds{};
};
struct Backend {
    std::function<bool(Record const&)> matches;
    std::function<bool(Record const&)> owner_running;
    std::function<bool(RECT&)> area;
    std::function<bool(RECT,RECT)> restore_area;
    std::function<int()> empty_region; // -1 means query failed
    std::function<bool(bool,RECT)> restore_region;
    std::function<bool()> topmost;
    std::function<bool()> restore_topmost;
};
std::filesystem::path receipt() {return std::filesystem::path(g_app.data)/L"native-taskbar-visibility.json";}
uint64_t created(DWORD pid) {
    unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!process) return 0;
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process.get(),&c,&e,&k,&u)) return 0;return (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
std::wstring rect_json(RECT const& r) {return L"["+std::to_wstring(r.left)+L","+std::to_wstring(r.top)+L","+std::to_wstring(r.right)+L","+std::to_wstring(r.bottom)+L"]";}
bool save(std::filesystem::path const& path,Record const& r) {
    std::wstring text=L"{\"schema\":1,\"window\":"+std::to_wstring(r.window)+L",\"shell\":"+std::to_wstring(r.shell)+L",\"shell_created\":"+std::to_wstring(r.shell_created)+L",\"owner\":"+std::to_wstring(r.owner)+L",\"owner_created\":"+std::to_wstring(r.owner_created)+L",\"work\":"+std::to_wstring(r.work)+L",\"region\":"+std::to_wstring(r.region)+L",\"topmost\":"+std::to_wstring(r.topmost)+L",\"region_present\":"+std::to_wstring(r.region_present)+L",\"before\":"+rect_json(r.before)+L",\"after\":"+rect_json(r.after)+L",\"bounds\":"+rect_json(r.bounds)+L"}\n";
    return write_utf8_atomic(path.c_str(),text.c_str())!=FALSE;
}
bool read_rect(Json const& json,char const* key,RECT& r) {
    int index=json_property(&json,0,key);if(index<0||json.tokens[index].type!=JSON_ARRAY||json.tokens[index].count!=4) return false;
    LONG* values[]={&r.left,&r.top,&r.right,&r.bottom};index++;
    for(int n=0;n<4;++n) {auto value=json_integer(&json,index,INT64_MAX);if(value<INT32_MIN||value>INT32_MAX) return false;*values[n]=LONG(value);index=json.tokens[index].next;}
    return r.right>=r.left&&r.bottom>=r.top;
}
bool load(std::filesystem::path const& path,Record& r) {
    Json json;if(!json_file(&json,path.c_str(),8192)) return false;
    auto integer=[&](char const* key){return json_integer(&json,json_property(&json,0,key),-1);};
    if(integer("schema")!=1) return false;
    auto window=integer("window"),shell=integer("shell"),sc=integer("shell_created"),owner=integer("owner"),oc=integer("owner_created");
    if(window<=0||shell<=0||shell>UINT32_MAX||sc<=0||owner<=0||owner>UINT32_MAX||oc<=0) return false;
    r.window=uint64_t(window);r.shell=DWORD(shell);r.shell_created=uint64_t(sc);r.owner=DWORD(owner);r.owner_created=uint64_t(oc);
    bool* flags[]={&r.work,&r.region,&r.topmost,&r.region_present};char const* keys[]={"work","region","topmost","region_present"};
    for(int n=0;n<4;++n) {auto value=integer(keys[n]);if(value!=0&&value!=1) return false;*flags[n]=value!=0;}
    return read_rect(json,"before",r.before)&&read_rect(json,"after",r.after)&&read_rect(json,"bounds",r.bounds);
}
bool recover(Record const& record,Backend const& backend) {
    if(backend.owner_running(record)) return false;
    // HWND reuse, an Explorer restart or a different user's session is not our
    // taskbar. Do not restore stale screen geometry into that new shell.
    if(!backend.matches(record)) return true;
    bool ok=true;
    if(record.region) {int empty=backend.empty_region();if(empty<0) ok=false;else if(empty&&!backend.restore_region(record.region_present,record.bounds)) ok=false;}
    if(record.work) {RECT current{};if(!backend.area(current)) ok=false;else if(EqualRect(&current,&record.after)&&!backend.restore_area(record.after,record.before)) ok=false;}
    if(record.topmost&&backend.topmost()&&!backend.restore_topmost()) ok=false;
    return ok;
}
struct AreaChange {RECT before,after;};
BOOL CALLBACK adjust_maximized(HWND window,LPARAM value) {
    if(!IsWindowVisible(window)||IsIconic(window)||GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT))return TRUE;auto change=reinterpret_cast<AreaChange*>(value);RECT frame{};
    if(SUCCEEDED(DwmGetWindowAttribute(window,9,&frame,sizeof frame))&&work_area_frame(frame,IsZoomed(window)!=FALSE,change->before,change->after)) {RECT bounds{};GetWindowRect(window,&bounds);SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top+change->after.bottom-frame.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING|SWP_ASYNCWINDOWPOS);}
    return TRUE;
}
Backend system_backend(Record const& record,bool allow_current) {
    HWND bar=HWND(uintptr_t(record.window));
    return {
        [](Record const& r){HWND window=HWND(uintptr_t(r.window));DWORD pid{};GetWindowThreadProcessId(window,&pid);return IsWindow(window)&&FindWindowW(L"Shell_TrayWnd",nullptr)==window&&pid==r.shell&&created(pid)==r.shell_created;},
        [allow_current](Record const& r){if(allow_current&&r.owner==GetCurrentProcessId()&&created(r.owner)==r.owner_created) return false;unique_handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,r.owner));if(!process) return GetLastError()==ERROR_ACCESS_DENIED;return created(r.owner)==r.owner_created&&WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT;},
        [](RECT& area){return SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0)!=FALSE;},
        [](RECT before,RECT after){if(!write_work_area(after)) return false;AreaChange change{before,after};EnumWindows(adjust_maximized,LPARAM(&change));return true;},
        [bar](){unique_gdi<HRGN> region(CreateRectRgn(0,0,0,0));if(!region) return -1;int type=GetWindowRgn(bar,region.get());if(type==ERROR) return 0;return type==NULLREGION?1:0;},
        // Only full-window regions are leased. A stale pixel-size snapshot can
        // permanently cut off the tray after the monitor changes dimensions.
        [bar](bool,RECT){return SetWindowRgn(bar,nullptr,TRUE)!=0;},
        [bar]{return (GetWindowLongPtrW(bar,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;},
        [bar]{return SetWindowPos(bar,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;}
    };
}
bool finish(std::filesystem::path const& path,Record const& record,bool allow_current) {
    if(!recover(record,system_backend(record,allow_current))) return false;
    std::error_code error;std::filesystem::remove(path,error);return !error;
}
}
struct RecoveryLease::Data {Record record;bool initialized{};std::filesystem::path path{receipt()};
    bool initialize(HWND bar) {
        if(initialized) return record.window==uint64_t(uintptr_t(bar));
        record.window=uint64_t(uintptr_t(bar));GetWindowThreadProcessId(bar,&record.shell);record.shell_created=created(record.shell);record.owner=GetCurrentProcessId();record.owner_created=created(record.owner);
        if(!record.window||!record.shell||!record.shell_created||!record.owner_created) return false;
        initialized=true;return true;
    }
};
RecoveryLease::RecoveryLease():data_(std::make_unique<Data>()) {}
RecoveryLease::~RecoveryLease() {restore();}
bool RecoveryLease::work(HWND bar,RECT before,RECT after) {if(!data_->initialize(bar)) return false;auto next=data_->record;if(!next.work) {next.before=before;next.after=after;next.work=true;}if(!save(data_->path,next)) return false;data_->record=next;return true;}
bool RecoveryLease::region(HWND bar,int kind,RECT bounds) {if(!data_->initialize(bar)) return false;auto next=data_->record;if(!next.region) {next.region=true;next.region_present=kind!=ERROR;next.bounds=bounds;}if(!save(data_->path,next)) return false;data_->record=next;return true;}
bool RecoveryLease::topmost(HWND bar) {if(!data_->initialize(bar)) return false;auto next=data_->record;next.topmost=true;if(!save(data_->path,next)) return false;data_->record=next;return true;}
bool RecoveryLease::restore() noexcept {try {if(!data_||!data_->initialized) return true;if(!finish(data_->path,data_->record,true)) return false;data_->record={};data_->initialized=false;return true;}catch(...) {return false;}}
bool recovery_pending() {std::error_code error;return std::filesystem::exists(receipt(),error);}
bool recover_visibility() noexcept {try {if(!recovery_pending()) return true;Record record;if(!load(receipt(),record)) return false;return finish(receipt(),record,false);}catch(...) {return false;}}
bool recovery_tests(std::wstring const& directory) {
    try {
        std::filesystem::create_directories(directory);auto path=std::filesystem::path(directory)/L"visibility.json";
        Record record{10,100,200,20,30,true,true,true,true,RECT{0,0,1920,1040},RECT{0,0,1920,1080},RECT{0,1040,1920,1080}};
        if(!save(path,record)) return false;Record read;if(!load(path,read)||read.window!=10||read.shell_created!=100||!EqualRect(&read.before,&record.before)) return false;
        bool match=true,running=false,empty=true,top=true,fail=false;RECT area=read.after;int mutations{};
        Backend fake{[&](Record const&){return match;},[&](Record const&){return running;},[&](RECT& current){current=area;return true;},[&](RECT,RECT value){if(fail) return false;area=value;++mutations;return true;},[&]{return empty?1:0;},[&](bool,RECT){empty=false;++mutations;return true;},[&]{return top;},[&]{top=false;++mutations;return true;}};
        running=true;if(recover(read,fake)||mutations) return false;running=false;match=false;if(!recover(read,fake)||mutations) return false;match=true;
        if(!recover(read,fake)||mutations!=3||empty||top||!EqualRect(&area,&read.before)) return false;
        if(!recover(read,fake)||mutations!=3) return false; // already restored is idempotent
        area={100,100,1800,900};if(!recover(read,fake)||mutations!=3||area.left!=100) return false; // preserve someone else's new work area
        area=read.after;fail=true;if(recover(read,fake)||mutations!=3) return false;
        fail=false;if(!recover(read,fake)||mutations!=4) return false;
        if(!write_utf8_atomic(path.c_str(),L"{\"schema\":1,\"window\":1}")) return false;return !load(path,read);
    }catch(...) {return false;}
}
}
