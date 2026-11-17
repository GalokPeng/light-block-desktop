#include "thumbnails.hpp"
#include "media_process.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <set>
namespace lb::thumbnails {
namespace {
struct Reply { std::wstring path;unique_gdi<HBITMAP> bitmap; };
struct Queue {
    std::mutex mutex;std::condition_variable changed;
    std::deque<std::wstring> requests;std::set<std::wstring> pending;
    std::vector<Reply> replies;std::thread thread;media::Control control;
    std::atomic_bool done{};bool stop{};
};
std::unique_ptr<Queue> queue;
// 32 decoded 400x225 covers cap native pixel residency near 11 MiB.
// The visible Qt grid has its own smaller cache; neither grows with library size.
constexpr size_t cache_limit=32;
std::deque<std::wstring> recent;
std::set<std::wstring> failed;
void touch(std::wstring const& path){auto it=std::find(recent.begin(),recent.end(),path);if(it!=recent.end())recent.erase(it);recent.push_back(path);while(recent.size()>cache_limit){auto oldest=std::move(recent.front());recent.pop_front();for(auto& item:g_app.items)if(oldest==item.path&&item.thumb){DeleteObject(item.thumb);item.thumb=nullptr;++g_app.thumbnail_revision;break;}}}

unique_gdi<HBITMAP> load(std::wstring const& path,media::Control &control) {
    if(media_kind(path.c_str())!=MEDIA_VIDEO) return unique_gdi<HBITMAP>(image_thumbnail(path.c_str(),400,225));
    WIN32_FILE_ATTRIBUTE_DATA information{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&information)) return {};
    uint64_t key=14695981039346656037ull;for(wchar_t ch:path) key=(key^uint64_t(towlower(ch)))*1099511628211ull;
    key^=(uint64_t(information.ftLastWriteTime.dwHighDateTime)<<32)|information.ftLastWriteTime.dwLowDateTime;key^=(uint64_t(information.nFileSizeHigh)<<32)|information.nFileSizeLow;
    auto directory=std::filesystem::path(g_app.data)/L"thumbnails";if(!ensure_directory(directory.c_str())) return {};
    auto cached=directory/(std::to_wstring(key)+L".png");
    if(GetFileAttributesW(cached.c_str())!=INVALID_FILE_ATTRIBUTES) { auto bitmap=unique_gdi<HBITMAP>(image_thumbnail(cached.c_str(),400,225));if(bitmap) return bitmap; }
    auto temporary=cached;temporary+=L".partial";
    scope_exit clean([&]{std::error_code ignored;std::filesystem::remove(temporary,ignored);});
    control.timeout_ms=6000;auto result=media::run({L"-i",path,L"-an",L"-frames:v",L"1",L"-vf",L"scale=400:225:force_original_aspect_ratio=decrease",L"-update",L"1",L"-f",L"image2",L"-c:v",L"png",temporary.wstring()},directory/L"decoder.log",control);
    if(FAILED(result.error)) return {};if(!MoveFileExW(temporary.c_str(),cached.c_str(),MOVEFILE_REPLACE_EXISTING)) return {};
    return unique_gdi<HBITMAP>(image_thumbnail(cached.c_str(),400,225));
}
}
void start() {
    if(queue||g_app.close_requested) return;queue=std::make_unique<Queue>();auto state=queue.get();state->thread=std::thread([state] {
        com_apartment apartment(COINIT_MULTITHREADED);scope_exit done([&]{state->done=true;});
        for(;;) {
            std::wstring path;{ std::unique_lock lock(state->mutex);state->changed.wait(lock,[&]{return state->stop||!state->requests.empty();});if(state->stop) return;path=std::move(state->requests.front());state->requests.pop_front();state->control.cancel=false; }
            Reply reply;reply.path=path;try { reply.bitmap=load(path,state->control); }catch(...) {}
            { std::lock_guard lock(state->mutex);if(state->stop) return;state->replies.push_back(std::move(reply)); }
            PostMessageW(g_app.window,WM_APP+98,0,0);
        }
    });
}
void request(std::wstring path) { if(g_app.close_requested)return;if(!queue) start();if(!queue)return;{ std::lock_guard lock(queue->mutex);if(queue->stop||queue->pending.size()>=cache_limit||failed.contains(path)||!queue->pending.insert(path).second) return;queue->requests.push_back(std::move(path)); }queue->changed.notify_one(); }
void receive() {
    if(!queue) return;std::vector<Reply> replies;{ std::lock_guard lock(queue->mutex);replies=std::move(queue->replies);for(auto const& reply:replies) queue->pending.erase(reply.path); }
    for(auto& reply:replies){if(!reply.bitmap){failed.insert(reply.path);continue;}for(auto& item:g_app.items)if(reply.path==item.path){if(item.thumb)DeleteObject(item.thumb);item.thumb=reply.bitmap.release();touch(reply.path);++g_app.thumbnail_revision;break;}}

    InvalidateRect(g_app.window,nullptr,FALSE);
}
HBITMAP get(int index){if(index<0||index>=int(g_app.count))return nullptr;auto& item=g_app.items[index];if(item.thumb){touch(item.path);return item.thumb;}request(item.path);return nullptr;}
bool cache_tests(){if(queue||!g_app.items.empty())return false;auto closing=g_app.close_requested;g_app.close_requested=TRUE;request(L"late-shutdown-request.png");start();g_app.close_requested=closing;if(queue)return false;auto count=g_app.count;auto revision=g_app.thumbnail_revision;scope_exit restore([&]{for(auto& item:g_app.items)if(item.thumb)DeleteObject(item.thumb);g_app.items.clear();g_app.count=count;g_app.thumbnail_revision=revision;recent.clear();});for(int n=0;n<140;++n){Item item{};StringCchPrintfW(item.path,LB_PATH,L"cache-fixture-%d",n);item.thumb=CreateBitmap(1,1,1,32,nullptr);if(!item.thumb)return false;g_app.items.push_back(item);++g_app.count;if(!get(n))return false;}size_t held=0;for(auto& item:g_app.items)held+=item.thumb!=nullptr;return held==cache_limit&&!g_app.items.front().thumb&&g_app.items.back().thumb&&recent.size()==cache_limit;}
bool shutdown() {
    if(!queue) return true;{ std::lock_guard lock(queue->mutex);queue->stop=true;queue->control.cancel=true; }queue->changed.notify_all();if(!queue->done) return false;if(queue->thread.joinable()) queue->thread.join();queue.reset();recent.clear();failed.clear();return true;
}
}
