#include "organizer.hpp"
#include "json.h"
#include <exdisp.h>
#include <shobjidl.h>
#include <servprov.h>

namespace lb::organizer {
namespace {
constexpr UINT ready_message=WM_APP+90,error_message=WM_APP+91;
winrt::com_ptr<IFolderView2> folder_view() {
    winrt::com_ptr<IShellWindows> windows;winrt::check_hresult(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_IShellWindows,windows.put_void()));
    VARIANT location{},root{};location.vt=VT_I4;location.lVal=CSIDL_DESKTOP;LONG hwnd=0;winrt::com_ptr<IDispatch> desktop;
    winrt::check_hresult(windows->FindWindowSW(&location,&root,SWC_DESKTOP,&hwnd,SWFO_NEEDDISPATCH,desktop.put()));if(!desktop) throw winrt::hresult_error(E_PENDING);
    auto provider=desktop.as<IServiceProvider>();winrt::com_ptr<IShellBrowser> browser;
    constexpr GUID top_level_browser{0x4c96be40,0x915c,0x11cf,{0x99,0xd3,0x00,0xaa,0x00,0x4a,0xe8,0x37}};
    winrt::check_hresult(provider->QueryService(top_level_browser,IID_IShellBrowser,browser.put_void()));winrt::com_ptr<IShellView> view;winrt::check_hresult(browser->QueryActiveShellView(view.put()));return view.as<IFolderView2>();
}
bool hidden(IFolderView2 *view) { DWORD flags=0;winrt::check_hresult(view->GetCurrentFolderFlags(&flags));if(!(flags&FWF_DESKTOP)) throw winrt::hresult_error(E_NOINTERFACE);return (flags&FWF_NOICONS)!=0; }
void set_hidden(IFolderView2 *view,bool value) { if(hidden(view)!=value) winrt::check_hresult(view->SetCurrentFolderFlags(FWF_NOICONS,value?FWF_NOICONS:0)); }
struct ExplorerVisibility {
    bool read() const {return hidden(folder_view().get());}
    void write(bool value) const {
        auto view=folder_view();
        set_hidden(view.get(),value);
        if(hidden(view.get())!=value) throw winrt::hresult_error(E_FAIL);
    }
};
template<class Visibility=ExplorerVisibility> struct IconLease {
    std::filesystem::path receipt;
    bool original{},owned{};
    Visibility desktop;
    explicit IconLease(std::filesystem::path data,Visibility visibility={}):receipt(std::move(data)/L"native-desktop-icons.json"),desktop(visibility) {}
    bool read_receipt() {
        Json json;
        if(!json_file(&json,receipt.c_str(),4096)) return false;
        int value=json_property(&json,0,"original_hidden");
        if(value<0 || (json.tokens[value].type!=JSON_TRUE && json.tokens[value].type!=JSON_FALSE)) return false;
        original=json.tokens[value].type==JSON_TRUE;
        return true;
    }
    void prepare() {
        std::error_code error;
        if(std::filesystem::exists(receipt,error)) {
            if(!read_receipt()) throw winrt::hresult_error(E_INVALIDARG);
            owned=true;
            if(!restore()) throw winrt::hresult_error(E_FAIL);
        }else if(error) throw winrt::hresult_error(E_ACCESSDENIED);
        desktop.read(); // Verify this is the desktop, without changing it.
    }
    void claim() {
        original=desktop.read();
        auto text=original?L"{\"schema\":1,\"original_hidden\":true}\n":L"{\"schema\":1,\"original_hidden\":false}\n";
        if(!write_utf8_atomic(receipt.c_str(),text)) throw winrt::hresult_error(E_ACCESSDENIED);
        owned=true;desktop.write(true);
    }
    bool restore() noexcept {
        if(!owned) return true;
        try {
            desktop.write(original);
            std::error_code error;std::filesystem::remove(receipt,error);
            if(error) return false;
            owned=false;return true;
        }catch(...) { return false; }
    }
    ~IconLease() { if(owned && !restore()) log_line(L"desktop icon recovery remains pending"); }
};
}
bool visibility_tests(std::filesystem::path const& root) {
    struct SimulatedDesktop {
        bool &value,&offline;
        bool read() const {if(offline) throw winrt::hresult_error(E_PENDING);return value;}
        void write(bool next) const {read();value=next;}
    };
    std::error_code error;std::filesystem::create_directories(root,error);if(error)return false;
    bool hidden_flag=false,offline=false;
    IconLease icons(root,SimulatedDesktop{hidden_flag,offline});
    for(bool initial:{false,true}) {
        hidden_flag=initial;icons.prepare();
        if(hidden_flag!=initial||std::filesystem::exists(icons.receipt)) return false;
        icons.claim();if(!hidden_flag||!std::filesystem::exists(icons.receipt)) return false;
        offline=true;if(icons.restore()||!std::filesystem::exists(icons.receipt))return false;
        offline=false;
        if(!icons.restore()||hidden_flag!=initial||std::filesystem::exists(icons.receipt)||!icons.restore())return false;
    }
    hidden_flag=true;
    if(!write_utf8_atomic(icons.receipt.c_str(),L"{\"schema\":1,\"original_hidden\":false}"))return false;
    icons.prepare();
    return !hidden_flag&&!std::filesystem::exists(icons.receipt);
}
int lease_main(int argc,wchar_t **argv) {
    HWND owner=nullptr;DWORD token=0,parent_pid=0;ULONGLONG parent_created=0;bool restore_only=false;
    for(int n=1;n<argc;++n) { if(!wcscmp(argv[n],L"--owner")&&n+1<argc) owner=reinterpret_cast<HWND>(uintptr_t(_wcstoui64(argv[++n],nullptr,10)));else if(!wcscmp(argv[n],L"--token")&&n+1<argc) token=wcstoul(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--parent-pid")&&n+1<argc) parent_pid=wcstoul(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--parent-created")&&n+1<argc) parent_created=_wcstoui64(argv[++n],nullptr,10);else if(!wcscmp(argv[n],L"--restore-only")) restore_only=true; }
    auto failed=[&](HRESULT result) { if(owner) PostMessageW(owner,error_message,token,LPARAM(result));return 2; };
    try {
        unique_handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent_pid));if(!parent) return failed(E_INVALIDARG);
        FILETIME created{},exit{},kernel{},user{};if(!GetProcessTimes(parent.get(),&created,&exit,&kernel,&user)||((ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=parent_created) return failed(E_INVALIDARG);
        unique_handle lease(CreateMutexW(nullptr,FALSE,L"Local\\Still.DesktopIconVisibility"));bool busy=GetLastError()==ERROR_ALREADY_EXISTS;if(!lease||busy) return failed(HRESULT_FROM_WIN32(ERROR_BUSY));
        com_apartment apartment;if(FAILED(apartment.result())) return failed(apartment.result());IconLease icons(g_app.data);
        if(restore_only) {
            if(!icons.read_receipt()) return failed(E_INVALIDARG);
            icons.owned=true;return icons.restore()?0:failed(E_FAIL);
        }
        auto stop_name=L"Local\\LightBlockNative.IconStop."+std::to_wstring(parent_pid);unique_handle stop(OpenEventW(SYNCHRONIZE,FALSE,stop_name.c_str()));if(!stop) return failed(E_INVALIDARG);
        HANDLE waits[]={parent.get(),stop.get()};
        auto ready_name=L"Local\\LightBlockNative.ShelfReady."+std::to_wstring(parent_pid)+L"."+std::to_wstring(token);
        unique_handle ready(OpenEventW(SYNCHRONIZE,FALSE,ready_name.c_str()));
        if(!ready) return failed(E_INVALIDARG);
        for(int attempt=0;;++attempt) {
            try {icons.prepare();break;}catch(winrt::hresult_error const&) {if(attempt>=7) throw;}
            auto deadline=GetTickCount64()+300;
            for(auto now=GetTickCount64();now<deadline;now=GetTickCount64()) {
                auto waited=MsgWaitForMultipleObjects(2,waits,FALSE,DWORD(deadline-now),QS_ALLINPUT);
                if(waited==WAIT_OBJECT_0||waited==WAIT_OBJECT_0+1) return 0;
                MSG pending;while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&pending);DispatchMessageW(&pending);}
            }
        }
        if(!owner||!PostMessageW(owner,ready_message,token,0)) return failed(E_FAIL);
        HANDLE handshake[]={parent.get(),stop.get(),ready.get()};
        DWORD acknowledged{};
        auto hr=CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS|COWAIT_DISPATCH_WINDOW_MESSAGES,30000,3,handshake,&acknowledged);
        if(FAILED(hr)) return failed(hr);
        if(acknowledged!=2) return 0;
        icons.claim();
        for(;;) {
            DWORD result=MsgWaitForMultipleObjects(2,waits,FALSE,INFINITE,QS_ALLINPUT);if(result==WAIT_OBJECT_0||result==WAIT_OBJECT_0+1) break;
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message);DispatchMessageW(&message); }
        }
        for(int attempt=0;attempt<8;++attempt) { if(icons.restore()) return 0;Sleep(250); }
        return failed(E_FAIL);
    } catch(winrt::hresult_error const& error) { log_line(L"desktop lease failed 0x%08lX",DWORD(error.code()));return failed(error.code()); }
      catch(...) { log_line(L"desktop lease failed; recovery state retained");return failed(E_FAIL); }
}
}
