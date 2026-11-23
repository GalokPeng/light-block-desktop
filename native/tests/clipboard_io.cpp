// Exercise the actual worker and publisher on a private Windows clipboard.
#include "../../src/clipboard.cpp"
#include <cstdio>
#include <fstream>
#include <future>

namespace {
using namespace lb;
using namespace lb::clipboard;
char const* stage="private window station";
void require(bool value,char const* message) { if(!value) throw std::runtime_error(message); }
std::vector<BYTE> file_bytes(std::filesystem::path const& path) {
    std::ifstream input(path,std::ios::binary);
    require(input.good(),"fixture file missing");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void write_format(HWND owner,UINT format,std::vector<BYTE> const& bytes) {
    HGLOBAL block=clipboard_block(bytes.data(),bytes.size());require(block,"allocate fixture");
    require(OpenClipboard(owner),"open fixture clipboard");
    scope_exit close([&]{CloseClipboard();});
    require(EmptyClipboard()&&SetClipboardData(format,block),"publish fixture");
}
void read_external() {
    require(OpenClipboard(nullptr),"open from second process");
    scope_exit close([&]{CloseClipboard();});
    require(GetClipboardData(CF_BITMAP)&&GetClipboardData(CF_DIB)&&GetClipboardData(CF_DIBV5),"standard Windows formats");
    auto bytes=registered_png(LB_CLIPBOARD_IMAGE_BYTES);
    require(!bytes.empty()&&bool(decode(bytes)),"read original PNG from second process");
}
void run(std::filesystem::path const& root,wchar_t const* exe) {
    auto name=L"LightBlockClipboardQA"+std::to_wstring(GetCurrentProcessId());
    auto station=CreateWindowStationW(name.c_str(),0,WINSTA_ALL_ACCESS,nullptr);
    require(station&&SetProcessWindowStation(station),"initialize isolated window station");
    auto desktop=CreateDesktopW(L"Test",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    require(desktop&&SetThreadDesktop(desktop),"initialize isolated desktop");
    com_apartment com(COINIT_MULTITHREADED);
    HWND owner=CreateWindowExW(0,L"STATIC",L"QA",WS_POPUP,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
    require(owner!=nullptr,"create clipboard owner");g_app.window=owner;
    StringCchCopyW(g_app.data,LB_PATH,root.c_str());
    stage="PNG-only history and editing";
    UINT png=RegisterClipboardFormatW(L"PNG");
    auto sample=file_bytes(root/L"transparent.png");
    for(auto format:{png,RegisterClipboardFormatW(L"image/png")}) {
        write_format(owner,format,sample);
        auto item=snapshot(GetClipboardSequenceNumber());
        require(item&&item->kind&&item->image==sample,"PNG-only history preserves bytes");
        unique_gdi<HBITMAP> bitmap;snapshot(GetClipboardSequenceNumber(),&bitmap);
        DIBSECTION dib{};require(bitmap&&GetObjectW(bitmap.get(),sizeof dib,&dib)==sizeof dib,"editor PNG DIB");
        require(dib.dsBm.bmWidth==2&&static_cast<BYTE*>(dib.dsBm.bmBits)[3]==127,"editor dimensions and alpha");
    }
    stage="open editor from PNG-only clipboard";
    auto sequence=GetClipboardSequenceNumber();
    capture::edit_clipboard(owner);
    HWND pin{};auto deadline=GetTickCount64()+5000;
    while(!pin&&GetTickCount64()<deadline) {
        Sleep(5);message(completed,0,0);
        pin=FindWindowW(L"LightBlockDesktopNative.Pin",nullptr);
    }
    require(pin&&GetClipboardSequenceNumber()==sequence,"edit opens a pin and preserves the original clipboard");
    DestroyWindow(pin);
    while(!shutdown()) Sleep(5);
    stage="respect clipboard privacy flags";
    auto flag=RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    DWORD exclude=0;
    require(OpenClipboard(owner),"open protected fixture");
    auto data=clipboard_block(&exclude,sizeof exclude);
    require(data&&SetClipboardData(flag,data),"set history exclusion flag");
    CloseClipboard();
    require(!snapshot(GetClipboardSequenceNumber()),"protected PNG is not retained");
    write_format(owner,png,sample);
    stage="history publication";
    Reply reply;reply.copied=snapshot(GetClipboardSequenceNumber());
    reply.bitmap.reset(reinterpret_cast<HBITMAP>(CreateSolidBrush(RGB(0,0,0))));
    auto before=GetClipboardSequenceNumber();
    require(FAILED(publish(reply))&&before==GetClipboardSequenceNumber(),"conversion failure must preserve clipboard");
    reply.bitmap=decode(reply.copied->image);require(SUCCEEDED(publish(reply)),"publish image from history");
    std::wstring command=L"\""+std::wstring(exe)+L"\" --read";
    auto child_desktop=name+L"\\Test";STARTUPINFOW startup{sizeof startup};startup.lpDesktop=child_desktop.data();
    PROCESS_INFORMATION process{};
    require(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"launch external reader");
    unique_handle child(process.hProcess),thread(process.hThread);DWORD code{};
    require(WaitForSingleObject(child.get(),10000)==WAIT_OBJECT_0&&GetExitCodeProcess(child.get(),&code)&&code==0,"external reader");
    stage="locked clipboard and sequence changes";
    for(bool replace:{false,true}) {
        before=GetClipboardSequenceNumber();
        require(OpenClipboard(owner),"lock fixture clipboard");
        std::promise<void> started;auto ready=started.get_future();
        auto reader=std::async(std::launch::async,[&]{
            com_apartment apartment(COINIT_MULTITHREADED);
            started.set_value();
            return snapshot(before);
        });
        ready.wait();Sleep(160);
        if(replace) {EmptyClipboard();auto data=clipboard_block(L"new",8);SetClipboardData(CF_UNICODETEXT,data);}
        CloseClipboard();
        auto item=reader.get();
        require(replace?!item:item&&item->image==sample,"retry busy clipboard without collecting superseded content");
    }
    stage="long-image history";
    Store store(root/L"size-regression.sqlite3");
    for(auto name:{L"long.png",L"large.png"}) {
        auto bytes=file_bytes(root/name);write_format(owner,png,bytes);
        auto item=snapshot(GetClipboardSequenceNumber());require(item&&item->image==bytes&&store.add(*item),"long or large PNG retained");
    }
    stage="oversize feedback";
    scope_exit stop([]{while(!shutdown()) Sleep(5);});
    for(auto path:{L"oversize.png",L"oversize-bytes.png"}) {
        write_format(owner,png,file_bytes(root/path));
        before=GetClipboardSequenceNumber();
        request_ui(Action::capture,0,before);
        auto end=GetTickCount64()+5000;
        while(service.busy&&GetTickCount64()<end) {Sleep(5);message(completed,0,0);}
        require(!service.busy&&service.error&&service.status.find(L"16 MiB")!=std::wstring::npos,"oversize feedback");
        require(GetClipboardSequenceNumber()==before,"oversize history leaves system clipboard intact");
    }
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2) return 2;
        if(std::wstring_view(argv[1])==L"--read") {lb::com_apartment com(COINIT_MULTITHREADED);read_external();}
        else run(argv[1],argv[0]);
        return 0;
    } catch(winrt::hresult_error const& e) {fprintf(stderr,"Clipboard regression (%s): HRESULT %08lx\n",stage,long(e.code()));fflush(stderr);return 1;}
    catch(std::exception const& e) {fprintf(stderr,"Clipboard regression: %s\n",e.what());fflush(stderr);return 1;}
}
