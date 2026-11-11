// Synthetic application used by tools/test_update_flow.py, never distributed.
#include <windows.h>
#include <string>

int wmain(int argc, wchar_t** argv) {
    wchar_t destination[32768]{}, parentText[32]{};
    if (!GetEnvironmentVariableW(L"QINGKUAI_UPDATE_FIXTURE_LOG",destination,32768)) return 1;
    GetEnvironmentVariableW(L"QINGKUAI_UPDATE_PARENT_PID",parentText,32);
    auto parent=OpenProcess(SYNCHRONIZE,FALSE,wcstoul(parentText,nullptr,10));
    bool alive=parent && WaitForSingleObject(parent,0)==WAIT_TIMEOUT;
    if (parent) CloseHandle(parent);
    std::wstring message=alive ? L"parent-alive\n" : L"parent-exited\n";
    wchar_t reset[8]{};
    GetEnvironmentVariableW(L"PYINSTALLER_RESET_ENVIRONMENT",reset,8);
    message+=std::wstring(reset)==L"1" ? L"independent-environment\n" : L"unsafe-environment\n";
    wchar_t dllDirectory[32768]{};
    GetDllDirectoryW(32768,dllDirectory);
    message+=dllDirectory[0]==L'\0' ? L"clean-dll-search\n" : L"inherited-dll-search\n";
    for (int i=0; i<argc; ++i) message+=std::wstring(argv[i])+L"\n";
    auto size=WideCharToMultiByte(CP_UTF8,0,message.data(),int(message.size()),nullptr,0,nullptr,nullptr);
    std::string bytes(size,'\0');
    WideCharToMultiByte(CP_UTF8,0,message.data(),int(message.size()),bytes.data(),size,nullptr,nullptr);
    auto file=CreateFileW(destination,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return 2;
    DWORD written{};
    WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr);
    CloseHandle(file);
    return alive ? 3 : 0;
}
