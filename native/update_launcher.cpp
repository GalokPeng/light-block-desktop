// Detached updater. Holds a real parent process handle and launches Setup only
// after that process has exited normally. No shell, script or forced shutdown.
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <array>
#include <map>
#include <string>
#include <vector>

struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring quote(std::wstring const& value) {
    std::wstring result=L"\"";
    unsigned slashes=0;
    for (auto ch : value) {
        if (ch==L'\\') { ++slashes; continue; }
        if (ch==L'\"') result.append(slashes*2+1,L'\\');
        else result.append(slashes,L'\\');
        slashes=0;
        result+=ch;
    }
    result.append(slashes*2,L'\\');
    return result+L"\"";
}

bool run(std::wstring const& file, std::vector<std::wstring> const& arguments, PROCESS_INFORMATION& process) {
    std::wstring command=quote(file);
    for (auto const& argument : arguments) command+=L" "+quote(argument);
    STARTUPINFOW startup{sizeof(startup)};
    return CreateProcessW(file.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process);
}

bool verify(HANDLE file, std::wstring const& expected) {
    if (expected.size()!=64) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file,&size) || size.QuadPart<2 || size.QuadPart>128*1024*1024) return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    bool valid=false;
    if (BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
        std::array<unsigned char,64*1024> buffer{};
        std::array<unsigned char,32> digest{};
        DWORD bytes{};
        bool first=true, ok=true;
        while (ReadFile(file,buffer.data(),DWORD(buffer.size()),&bytes,nullptr) && bytes) {
            if (first && (bytes<2 || buffer[0]!='M' || buffer[1]!='Z')) { ok=false; break; }
            first=false;
            if (BCryptHashData(hash,buffer.data(),bytes,0)<0) { ok=false; break; }
        }
        if (ok && !first && BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)>=0) {
            constexpr wchar_t hex[]=L"0123456789abcdef";
            std::wstring actual;
            for (auto byte : digest) { actual+=hex[byte>>4]; actual+=hex[byte&15]; }
            valid=actual==expected;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm,0);
    return valid;
}

int wmain(int argc, wchar_t** argv) {
    // Do not leak the old frozen application's DLL search path or bootloader
    // identity into Setup or into the independent application restart.
    SetDllDirectoryW(nullptr);
    SetEnvironmentVariableW(L"PYINSTALLER_RESET_ENVIRONMENT",L"1");
    std::map<std::wstring,std::wstring> args;
    for (int i=1; i+1<argc; i+=2) args.emplace(argv[i],argv[i+1]);
    for (auto key : {L"--parent",L"--setup",L"--sha256",L"--directory",L"--profile",L"--language",L"--mode",L"--ready"}) {
        if (!args.contains(key)) return 1;
    }
    auto language=args[L"--language"];
    if (language!=L"english" && language!=L"chinesesimplified") return 1;
    bool silent=args[L"--mode"]==L"installed";
    auto pidText=args[L"--parent"];
    if (pidText.empty() || pidText.find_first_not_of(L"0123456789")!=std::wstring::npos) return 1;
    auto pid=wcstoul(pidText.c_str(),nullptr,10);
    Handle parent{OpenProcess(SYNCHRONIZE,FALSE,pid)};
    Handle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,args[L"--ready"].c_str())};
    if (!parent.value || !ready.value || WaitForSingleObject(parent.value,0)!=WAIT_TIMEOUT) return 2;
    Handle file{CreateFileW(args[L"--setup"].c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if (file.value==INVALID_HANDLE_VALUE || !verify(file.value,args[L"--sha256"])) return 4;
    std::vector<std::wstring> parameters{L"/SP-",L"/NORESTART",L"/LANG="+language};
    if (silent) {
        if (args[L"--directory"].empty()) return 1;
        parameters.insert(parameters.end(),{L"/SILENT",L"/SUPPRESSMSGBOXES",L"/NOFORCECLOSEAPPLICATIONS",
                                           L"/DIR="+args[L"--directory"]});
    }
    if (!SetEvent(ready.value)) return 3;
    if (WaitForSingleObject(parent.value,10*60*1000)!=WAIT_OBJECT_0) return 5;
    PROCESS_INFORMATION setup{};
    DWORD result=6;
    if (run(args[L"--setup"],parameters,setup)) {
        Handle process{setup.hProcess}, thread{setup.hThread};
        WaitForSingleObject(process.value,INFINITE);
        if (!GetExitCodeProcess(process.value,&result)) result=6;
    }
    if (result!=0 && result!=2) {
        MessageBoxW(nullptr,language==L"english" ? L"The update could not be installed. Try updating again or download Setup from the website."
                    : L"更新未能安装。请重试更新，或从官网下载最新安装包。",L"Light Block Desktop",MB_OK|MB_ICONWARNING);
    }
    // Interactive Setup owns its Launch checkbox. Silent upgrades restart the
    // same installation and profile, including when Setup was cancelled/failed.
    if (silent) {
        std::vector<std::wstring> launch;
        if (!args[L"--profile"].empty()) launch={L"--data-dir",args[L"--profile"]};
        PROCESS_INFORMATION app{};
        if (run(args[L"--directory"]+L"\\轻块桌面.exe",launch,app)) {
            CloseHandle(app.hThread);
            CloseHandle(app.hProcess);
        }
    }
    return int(result);
}
