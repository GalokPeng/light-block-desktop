#pragma once
#include "native.hpp"
#include <thread>
#include <array>

namespace lb {
class DirectoryWatch {
    struct Root {
        std::wstring path;
        unique_handle directory,event;
        OVERLAPPED operation{};
        std::array<BYTE,16384> buffer{};
        bool pending{},subtree{};
    };
    struct State {
        unique_handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        unique_handle done{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        std::vector<std::unique_ptr<Root>> roots;
        HWND target{};UINT message{};WPARAM token{};
    };
    std::shared_ptr<State> state_;
    std::thread thread_;
    static bool issue(Root &root) noexcept {
        ResetEvent(root.event.get());ZeroMemory(&root.operation,sizeof root.operation);root.operation.hEvent=root.event.get();
        root.pending=ReadDirectoryChangesW(root.directory.get(),root.buffer.data(),DWORD(root.buffer.size()),root.subtree,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES,nullptr,&root.operation,nullptr)!=FALSE;return root.pending;
    }
    static bool arm(Root &root) noexcept {
        if(!root.directory)root.directory.reset(CreateFileW(root.path.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr));
        if(root.directory&&issue(root))return true;
        root.directory.reset();ResetEvent(root.event.get());return false;
    }
public:
    DirectoryWatch()=default;
    ~DirectoryWatch() { stop(); }
    DirectoryWatch(DirectoryWatch const&)=delete;
    DirectoryWatch& operator=(DirectoryWatch const&)=delete;
    bool start(std::vector<std::wstring> const& roots,HWND target,UINT message,WPARAM token,bool subtree=false) {
        stop();auto state=std::make_shared<State>();if(!state->stop||!state->done) return false;state->target=target;state->message=message;state->token=token;
        for(auto const& path:roots) {
            if(state->roots.size()>=MAXIMUM_WAIT_OBJECTS-1)break;
            auto root=std::make_unique<Root>();root->path=path;root->subtree=subtree;root->event.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));
            if(root->event) {arm(*root);state->roots.push_back(std::move(root));}
        }
        if(state->roots.empty()) return false;
        state_=state;thread_=std::thread([state] {
            scope_exit finished([&]{SetEvent(state->done.get());});
            std::vector<HANDLE> events{state->stop.get()};for(auto &root:state->roots)events.push_back(root->event.get());
            for(;;) {
                bool retry=std::any_of(state->roots.begin(),state->roots.end(),[](auto const& root){return !root->pending;});
                DWORD event=WaitForMultipleObjects(DWORD(events.size()),events.data(),FALSE,retry?2000:INFINITE);if(event==WAIT_OBJECT_0||event==WAIT_FAILED) break;
                if(event==WAIT_TIMEOUT){bool recovered=false;for(auto& root:state->roots)if(!root->pending)recovered=arm(*root)||recovered;if(recovered)PostMessageW(state->target,state->message,state->token,0);continue;}
                size_t index=event-WAIT_OBJECT_0-1;if(index>=state->roots.size()) break;auto &root=*state->roots[index];DWORD bytes=0;
                if(root.pending) GetOverlappedResult(root.directory.get(),&root.operation,&bytes,FALSE);root.pending=false;
                if(WaitForSingleObject(state->stop.get(),0)==WAIT_OBJECT_0) break;
                // Re-arm before delivery. A disconnected or replaced root is
                // retried without taking healthy Desktop/Public Desktop down.
                if(!issue(root)){root.directory.reset();ResetEvent(root.event.get());}
                PostMessageW(state->target,state->message,state->token,0);
            }
            for(auto &root:state->roots) if(root->pending) { CancelIoEx(root->directory.get(),&root->operation);DWORD bytes=0;GetOverlappedResult(root->directory.get(),&root->operation,&bytes,TRUE);root->pending=false; }
        });return true;
    }
    void stop() noexcept {
        if(!state_) return;SetEvent(state_->stop.get());
        if(thread_.joinable()) {
            if(WaitForSingleObject(state_->done.get(),1000)==WAIT_OBJECT_0) thread_.join();
            else thread_.detach(); /* The thread retains its state and OVERLAPPED buffers until cancellation completes. */
        }
        state_.reset();
    }
};
}
