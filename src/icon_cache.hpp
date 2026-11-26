#pragma once
#include "lb.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <array>
#include <list>
#include <map>

namespace lb {
bool icon_cache_tests();
class IconCache {
    using Key=std::pair<std::wstring,int>;
    struct Request { Key key;std::wstring path;bool folder{},system{};int size{128}; };
    struct Reply { Key key;std::wstring path;HICON icon{};~Reply(){if(icon) DestroyIcon(icon);} };
    struct State {
        std::mutex mutex;std::condition_variable condition;
        std::deque<Request> requests;
        std::vector<std::unique_ptr<Reply>> replies;
        std::set<Key> pending;
        unique_handle done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        HWND window{};UINT message{};bool stop{},resolve_metadata{};size_t pending_limit{512};
    };
    std::shared_ptr<State> state_;
    std::thread thread_;
    struct Cached { HICON icon{};std::list<Key>::iterator position; };
    std::map<Key,Cached> icons_;
    std::list<Key> order_;
    size_t bytes_{};
    bool saturated_{};
    static size_t cost(int size){return size_t(size)*size*4+size_t((size+15)/16*2)*size;}
public:
    IconCache()=default;
    ~IconCache();
    void start(HWND window,UINT message,size_t pending_limit=512,bool resolve_metadata=false);
    void stop(DWORD wait_ms=1000) noexcept;
    HICON get(std::wstring const& key,std::wstring const& path,bool folder,bool system,int size=128);
    bool receive(std::set<std::wstring>* changed_paths=nullptr);
};
}
