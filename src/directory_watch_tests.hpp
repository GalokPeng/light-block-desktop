#pragma once
#include "directory_watch.hpp"
#include "lb.h"
namespace lb {
inline bool directory_watch_tests(std::filesystem::path const& directory){
    std::error_code error;auto desktop=directory/L"Desktop",late=directory/L"Public Desktop";
    std::filesystem::create_directories(desktop,error);if(error)return false;
    HWND window=CreateWindowExW(0,L"STATIC",L"Hidden directory notifications test",0,0,0,1,1,HWND_MESSAGE,nullptr,g_app.instance,nullptr);if(!window)return false;
    scope_exit destroy([&]{DestroyWindow(window);});DirectoryWatch watch;constexpr UINT changed=WM_APP+191;constexpr WPARAM token=123;
    if(!watch.start({desktop.wstring(),late.wstring()},window,changed,token))return false;
    auto wait=[&](DWORD timeout){auto until=GetTickCount64()+timeout;while(GetTickCount64()<until){MSG message;while(PeekMessageW(&message,window,changed,changed,PM_REMOVE))if(message.wParam==token)return true;MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_POSTMESSAGE);}return false;};
    // The watch is armed before start returns, not after the first thread runs.
    auto file=desktop/L"Immediate new item.txt";if(!write_utf8_atomic(file.c_str(),L"new item")||!wait(3000))return false;
    watch.stop();MSG message;while(PeekMessageW(&message,window,changed,changed,PM_REMOVE)){}
    if(!watch.start({late.wstring()},window,changed,token))return false;
    std::filesystem::create_directories(late,error);if(error)return false;
    if(!write_utf8_atomic((late/L"Downloaded item.txt").c_str(),L"passive arrival")||!wait(4500))return false;
    watch.stop();return true;
}
}
