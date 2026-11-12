// Original system cursor host. No registry writes; pipe EOF restores the scheme.
#include <windows.h>
#include <array>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

namespace {
struct Role { UINT id; wchar_t const* file; bool optional{}; };
constexpr std::array roles{
    Role{32512,L"normal.cur"},Role{32513,L"text.cur"},Role{32514,L"busy.ani"},
    Role{32515,L"cross.cur"},Role{32516,L"alternate.cur"},Role{32642,L"diagonal1.cur"},
    Role{32643,L"diagonal2.cur"},Role{32644,L"horizontal.cur"},Role{32645,L"vertical.cur"},
    Role{32646,L"move.cur"},Role{32648,L"unavailable.cur"},Role{32649,L"hand.cur"},
    Role{32650,L"working.ani"},Role{32651,L"help.cur"},Role{32671,L"pin.cur",true},
    Role{32672,L"person.cur",true},Role{32631,L"pen.cur",true}
};

void reply(std::string const& message) {
    DWORD written{};
    auto data=message+"\n";
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),data.data(),static_cast<DWORD>(data.size()),&written,nullptr);
}

std::wstring wide(std::string const& text) {
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (!size) return {};
    std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);
    return result;
}

uint64_t signature() {
    ICONINFO info{};
    auto cursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    if (!cursor || !GetIconInfo(cursor,&info)) return 0;
    uint64_t hash=1469598103934665603ULL;
    auto bitmap=info.hbmColor ? info.hbmColor : info.hbmMask;
    BITMAP details{};
    if (GetObjectW(bitmap,sizeof(details),&details) && details.bmWidth>0 && details.bmWidth<=512 && details.bmHeight>0 && details.bmHeight<=1024) {
        BITMAPINFO header{};
        header.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        header.bmiHeader.biWidth=details.bmWidth;
        header.bmiHeader.biHeight=-details.bmHeight;
        header.bmiHeader.biPlanes=1;header.bmiHeader.biBitCount=32;
        std::vector<uint32_t> pixels(static_cast<size_t>(details.bmWidth)*details.bmHeight);
        auto dc=GetDC(nullptr);
        if (GetDIBits(dc,bitmap,0,details.bmHeight,pixels.data(),&header,DIB_RGB_COLORS)) {
            hash^=details.bmWidth;hash*=1099511628211ULL;
            hash^=details.bmHeight;hash*=1099511628211ULL;
            for (auto value:pixels) { hash^=value;hash*=1099511628211ULL; }
        } else hash=0;
        ReleaseDC(nullptr,dc);
    } else hash=0;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return hash;
}

struct Cursors {
    bool applied{};
    uint64_t owned{};

    void restore(bool force=false) {
        if (force || (applied && signature()==owned))
            SystemParametersInfoW(SPI_SETCURSORS,0,nullptr,0);
        applied=false;
    }

    bool apply(std::wstring const& directory, int percent=100) {
        std::vector<std::pair<Role,HCURSOR>> loaded;
        auto user=GetModuleHandleW(L"user32.dll");
        auto dpi=reinterpret_cast<UINT(WINAPI*)()>(GetProcAddress(user,"GetDpiForSystem"));
        auto metrics=reinterpret_cast<int(WINAPI*)(int,UINT)>(GetProcAddress(user,"GetSystemMetricsForDpi"));
        int size=metrics && dpi ? metrics(SM_CXCURSOR,dpi()) : GetSystemMetrics(SM_CXCURSOR);
        if (size<16 || size>256) size=32;
        size=std::clamp((size*percent+50)/100,16,256);
        for (auto role:roles) {
            if (role.optional && !LoadCursorW(nullptr,MAKEINTRESOURCEW(role.id))) continue;
            auto path=directory+L"\\"+role.file;
            if (path.ends_with(L".ani")) {
                int variant=128;
                for (auto candidate : {32,48,64,96,128}) if (candidate>=size) { variant=candidate;break; }
                path.insert(path.size()-4,L"-"+std::to_wstring(variant));
            }
            auto cursor=static_cast<HCURSOR>(LoadImageW(nullptr,path.c_str(),IMAGE_CURSOR,size,size,LR_LOADFROMFILE));
            if (!cursor) {
                for (auto item:loaded) DestroyCursor(item.second);
                return false;
            }
            loaded.emplace_back(role,cursor);
        }
        bool changed=false;
        for (size_t n=0;n<loaded.size();++n) {
            auto [role,cursor]=loaded[n];
            if (!SetSystemCursor(cursor,role.id)) {
                DestroyCursor(cursor);
                if (role.optional) continue;
                for (++n;n<loaded.size();++n) DestroyCursor(loaded[n].second);
                if (changed) restore(true);
                return false;
            }
            changed=true;
        }
        applied=true;
        owned=signature();
        return true;
    }

    ~Cursors() { restore(); }
};
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR command,int) {
    if (command && std::wstring(command)==L"--probe") { reply("READY");return 0; }
    auto awareness=reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    if (awareness) awareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else SetProcessDPIAware();
    SetLastError(0);
    auto mutex=CreateMutexW(nullptr,TRUE,L"Local\\Qingkuai.PointerStyle");
    if (!mutex || GetLastError()==ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        reply("BUSY");return 1;
    }
    int result=0;
    {
        Cursors cursors;
        if (command && std::wstring(command)==L"--restore") {
            cursors.restore(true);
        } else {
            std::string pending;
            std::array<char,1024> buffer;
            DWORD read{};
            bool quit=false;
            while (!quit && ReadFile(GetStdHandle(STD_INPUT_HANDLE),buffer.data(),buffer.size(),&read,nullptr) && read) {
                pending.append(buffer.data(),read);
                if (pending.size()>65536) { result=1;break; }
                for (size_t newline;(newline=pending.find('\n'))!=std::string::npos;) {
                    auto line=pending.substr(0,newline);pending.erase(0,newline+1);
                    if (!line.empty() && line.back()=='\r') line.pop_back();
                    if (line=="QUIT") { quit=true;break; }
                    if (line=="RESTORE") { cursors.restore();reply("OK");continue; }
                    if (line.starts_with("APPLY\t")) {
                        auto argument=line.substr(6);
                        int percent=100;
                        bool valid=true;
                        auto separator=argument.rfind('\t');
                        if (separator!=std::string::npos) {
                            auto scale=argument.substr(separator+1);
                            auto [end,error]=std::from_chars(scale.data(),scale.data()+scale.size(),percent);
                            valid=error==std::errc{} && end==scale.data()+scale.size() && percent>=75 && percent<=200;
                            argument.resize(separator);
                        }
                        auto directory=wide(argument);
                        if (valid && !directory.empty() && cursors.apply(directory,percent)) reply("OK");
                        else reply("ERROR "+std::to_string(GetLastError()));
                    } else { reply("ERROR protocol");result=1;quit=true;break; }
                }
            }
        }
    }  // Restore before releasing ownership, including parent crash/pipe EOF.
    ReleaseMutex(mutex);CloseHandle(mutex);
    return result;
}
