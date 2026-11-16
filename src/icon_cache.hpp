#pragma once
#include "lb.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <array>

namespace lb {
class IconCache {
    struct Request { std::wstring key,path;bool folder{},system{}; };
    struct Reply { std::wstring key;HICON icon{};~Reply(){if(icon) DestroyIcon(icon);} };
    struct State {
        std::mutex mutex;std::condition_variable condition;
        std::deque<Request> requests;
        std::vector<std::unique_ptr<Reply>> replies;
        std::unordered_set<std::wstring> pending;
        unique_handle done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        HWND window{};UINT message{};bool stop{},resolve_metadata{};size_t pending_limit{512};
    };
    std::shared_ptr<State> state_;
    std::thread thread_;
    std::unordered_map<std::wstring,HICON> icons_;
    std::deque<std::wstring> order_;
public:
    IconCache()=default;
    ~IconCache();
    void start(HWND window,UINT message,size_t pending_limit=512,bool resolve_metadata=false);
    void stop(DWORD wait_ms=1000) noexcept;
    HICON get(std::wstring const& key,std::wstring const& path,bool folder,bool system);
    void receive();
};
}
