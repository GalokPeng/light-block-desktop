#include "clipboard.hpp"
#include "annotation.hpp"
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace lb::clipboard {
namespace {
constexpr UINT completed=WM_APP+130;
enum class Action { capture,list,copy,preview,pin,remove,clear,inspect,edit };
struct Job { Action action{};DWORD sequence{};int64_t id{};uint64_t query_id{};std::wstring query;bool ui{};uint64_t selection_id{}; };
struct Reply { std::vector<Item> items;std::optional<Item> copied;unique_gdi<HBITMAP> bitmap;uint64_t query_id{};DWORD sequence{};HRESULT error{S_OK};bool copy{},preview{},ui{};Action action{};uint64_t selection_id{}; };
struct Worker {
    std::mutex mutex;std::condition_variable condition;std::deque<Job> jobs;std::deque<Reply> replies;
    std::thread thread;std::atomic_bool done{},failed{};bool stop{};
};
std::unique_ptr<Worker> worker;
struct View { HWND window{},list{},search{},enabled{},status{};std::vector<Item> rows;bool active{},listening{};uint64_t query_id{};DWORD written{},captured{}; } view;
struct Service { std::vector<Item> rows;std::optional<Item> detail;std::wstring query,status;uint64_t query_id{},selection_id{};uint32_t revision{1},detail_revision{1};int64_t selected{};bool active{},busy{},detail_busy{},error{}; } service;
void request_ui(Action action,int64_t id=0,DWORD sequence=0);
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
std::filesystem::path directory() { auto path=std::filesystem::path(g_app.data)/L"clipboard";if(!ensure_directory(path.c_str())) throw winrt::hresult_error(E_ACCESSDENIED);return path; }
std::vector<BYTE> read_bytes(std::filesystem::path const& path) {
    unique_handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));LARGE_INTEGER size{};if(!file||!GetFileSizeEx(file.get(),&size)||size.QuadPart<=0) return {};
    if(size.QuadPart>LB_CLIPBOARD_IMAGE_BYTES) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    std::vector<BYTE> result(size_t(size.QuadPart));DWORD read=0;if(!ReadFile(file.get(),result.data(),DWORD(result.size()),&read,nullptr)||read!=result.size()) return {};return result;
}
bool excluded() {
    if(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"))) return true;
    for(auto name:{L"CanIncludeInClipboardHistory",L"CanUploadToCloudClipboard"}) {
        UINT format=RegisterClipboardFormatW(name);if(!IsClipboardFormatAvailable(format)) continue;HGLOBAL data=GetClipboardData(format);
        if(!data||GlobalSize(data)<sizeof(DWORD)) return true;auto pointer=static_cast<DWORD const*>(GlobalLock(data));if(!pointer) return true;DWORD flag=*pointer;GlobalUnlock(data);if(!flag) return true;
    }return false;
}
void check_pixels(uint64_t width,uint64_t height) {
    if(width && height>uint64_t(LB_CLIPBOARD_IMAGE_PIXELS)/width)
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
}
UINT32 big_endian(BYTE const* p) {
    return (UINT32(p[0])<<24)|(UINT32(p[1])<<16)|(UINT32(p[2])<<8)|p[3];
}
// GlobalSize may include allocator padding. Keep exactly the PNG through IEND.
std::vector<BYTE> registered_png(size_t limit) {
    for(auto name:{L"PNG",L"image/png"}) {
        UINT format=RegisterClipboardFormatW(name);
        if(!IsClipboardFormatAvailable(format)) continue;
        HGLOBAL handle=GetClipboardData(format);
        size_t size=handle?GlobalSize(handle):0;
        BYTE const* data=handle?static_cast<BYTE const*>(GlobalLock(handle)):nullptr;
        if(!data) continue;
        scope_exit unlock([&]{GlobalUnlock(handle);});
        static constexpr BYTE signature[]={137,80,78,71,13,10,26,10};
        if(size<33||memcmp(data,signature,8)||big_endian(data+8)!=13||memcmp(data+12,"IHDR",4)) continue;
        check_pixels(big_endian(data+16),big_endian(data+20));
        for(size_t offset=8;size-offset>=12;) {
            size_t length=big_endian(data+offset);
            if(length>size-offset-12) break;
            size_t end=offset+length+12;
            if(end>limit) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            if(!memcmp(data+offset+4,"IEND",4)) {
                if(!length) return {data,data+end};
                break;
            }
            offset=end;
        }
    }
    return {};
}
unique_gdi<HBITMAP> decode(std::vector<BYTE>& bytes) {
    if(bytes.empty()||bytes.size()>UINT_MAX) return {};
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::com_ptr<IWICStream> stream;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    winrt::com_ptr<IWICFormatConverter> converter;
    GUID format{};UINT width{},height{};
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,factory.put_void()))||
       FAILED(factory->CreateStream(stream.put()))||FAILED(stream->InitializeFromMemory(bytes.data(),UINT(bytes.size())))||
       FAILED(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnDemand,decoder.put()))||
       FAILED(decoder->GetContainerFormat(&format))||!IsEqualGUID(format,GUID_ContainerFormatPng)||
       FAILED(decoder->GetFrame(0,frame.put()))||FAILED(frame->GetSize(&width,&height))||!width||!height) return {};
    check_pixels(width,height);
    if(FAILED(factory->CreateFormatConverter(converter.put()))||
       FAILED(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(width);
    info.bmiHeader.biHeight=-LONG(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;unique_gdi<HBITMAP> result(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));
    if(!result||FAILED(converter->CopyPixels(nullptr,width*4,width*height*4,static_cast<BYTE*>(pixels)))) return {};
    return result;
}
unique_gdi<HBITMAP> dib_snapshot() {
    if(IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP source=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));BITMAP info{};if(source&&GetObjectW(source,sizeof info,&info)&&info.bmWidth>0&&info.bmHeight>0) {check_pixels(info.bmWidth,info.bmHeight);return unique_gdi<HBITMAP>(static_cast<HBITMAP>(CopyImage(source,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION)));}
    }
    UINT format=IsClipboardFormatAvailable(CF_DIBV5)?CF_DIBV5:CF_DIB;if(!IsClipboardFormatAvailable(format)) return {};
    HGLOBAL data=GetClipboardData(format);
    SIZE_T size=data?GlobalSize(data):0;
    if(size<sizeof(BITMAPINFOHEADER)) return {};
    if(size>size_t(LB_CLIPBOARD_IMAGE_PIXELS)*4+65536)
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    auto bytes=static_cast<BYTE const*>(GlobalLock(data));
    if(!bytes) return {};
    scope_exit unlock([&]{GlobalUnlock(data);});auto header=reinterpret_cast<BITMAPINFOHEADER const*>(bytes);
    if(header->biSize<40||header->biSize>size||header->biWidth<1||header->biHeight==LONG_MIN||header->biPlanes!=1||(header->biBitCount!=24&&header->biBitCount!=32)||(header->biCompression!=BI_RGB&&header->biCompression!=BI_BITFIELDS)) return {};
    int height=abs(header->biHeight);check_pixels(header->biWidth,height);if(!height||header->biClrUsed>256) return {};
    size_t offset=header->biSize+(header->biSize==40&&header->biCompression==BI_BITFIELDS?12:0)+size_t(header->biClrUsed)*4;uint64_t stride=((uint64_t(header->biWidth)*header->biBitCount+31)/32)*4;
    if(offset>size||stride*height>size-offset) return {};
    HDC dc=GetDC(nullptr);auto bitmap=unique_gdi<HBITMAP>(CreateDIBitmap(dc,header,CBM_INIT,bytes+offset,reinterpret_cast<BITMAPINFO const*>(bytes),DIB_RGB_COLORS));ReleaseDC(nullptr,dc);return bitmap;
}
// This runs on the clipboard worker. A newer copy supersedes a pending retry.
bool open_sequence(DWORD sequence) {
    for(unsigned delay:{0u,10u,25u,50u,100u,200u,300u,400u}) {
        if(delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if(GetClipboardSequenceNumber()!=sequence) return false;
        if(OpenClipboard(nullptr)) return true;
    }
    throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_BUSY));
}
std::optional<Item> snapshot(DWORD expected,unique_gdi<HBITMAP>* editor=nullptr,bool try_png=true) {
    if(!open_sequence(expected)) return {};
    unique_gdi<HBITMAP> bitmap;Item item;
    {
        scope_exit close([&]{CloseClipboard();});
        if(GetClipboardSequenceNumber()!=expected||(!editor&&excluded())) return {};
        if(try_png) item.image=registered_png(editor?size_t(LB_CLIPBOARD_IMAGE_PIXELS)*4+65536:LB_CLIPBOARD_IMAGE_BYTES);
        if(item.image.empty()) bitmap=dib_snapshot();
        if(item.image.empty()&&!bitmap) {
            if(editor||!IsClipboardFormatAvailable(CF_UNICODETEXT)) return {};
            HGLOBAL data=GetClipboardData(CF_UNICODETEXT);SIZE_T bytes=data?GlobalSize(data):0;if(bytes<sizeof(wchar_t)||bytes>5*1024*1024) return {};
            auto text=static_cast<wchar_t const*>(GlobalLock(data));if(!text) return {};scope_exit unlock([&]{GlobalUnlock(data);});
            size_t length=wcsnlen(text,bytes/sizeof(wchar_t));if(length==bytes/sizeof(wchar_t)) return {};item.text.assign(text,length);
            if(std::all_of(item.text.begin(),item.text.end(),[](wchar_t c){return iswspace(c)!=0;})) return {};
            return item;
        }
    }
    if(!item.image.empty()) {
        bitmap=decode(item.image);
        if(!bitmap) return snapshot(expected,editor,false);
    }
    if(editor) { *editor=std::move(bitmap);return {}; }
    if(item.image.empty()) {
        auto path=directory()/L"incoming.png";scope_exit clean([&]{DeleteFileW(path.c_str());});
        if(!save_png(bitmap.get(),path.c_str())) throw winrt::hresult_error(E_FAIL);
        item.image=read_bytes(path);if(item.image.empty()) throw winrt::hresult_error(E_FAIL);
    }
    BITMAP info{};GetObjectW(bitmap.get(),sizeof info,&info);item.kind=1;
    item.text=std::wstring(tr(L"图片 ",L"Image "))+std::to_wstring(info.bmWidth)+L" × "+std::to_wstring(info.bmHeight);
    return item;
}
void start_worker() {
    if(worker&&worker->done) { if(worker->thread.joinable()) worker->thread.join();worker.reset(); }
    if(worker) return;worker=std::make_unique<Worker>();auto state=worker.get();
    state->thread=std::thread([state] {
        com_apartment apartment(COINIT_MULTITHREADED);scope_exit done([&]{state->done=true;});
        try {
            std::optional<Store> store;
            for(;;) {
                Job job;{ std::unique_lock lock(state->mutex);state->condition.wait(lock,[&]{return state->stop||!state->jobs.empty();});if(state->stop) return;job=std::move(state->jobs.front());state->jobs.pop_front(); }
                Reply reply;reply.sequence=job.sequence;reply.query_id=job.query_id;reply.ui=job.ui;reply.action=job.action;reply.selection_id=job.selection_id;
                try {
                    if(job.action!=Action::edit&&!store) store.emplace(directory()/L"history.sqlite3");
                    if(job.action==Action::edit) snapshot(job.sequence,&reply.bitmap);
                    else if(job.action==Action::capture) { auto item=snapshot(job.sequence);if(item&&!store->add(*item)) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)); }
                    else if(job.action==Action::pin) store->toggle_pin(job.id);
                    else if(job.action==Action::remove) store->remove(job.id);else if(job.action==Action::clear) store->clear();
                    else if(job.action==Action::copy||job.action==Action::preview||job.action==Action::inspect) { reply.copied=store->get(job.id);reply.copy=job.action==Action::copy;reply.preview=job.action==Action::preview;if(job.action!=Action::inspect&&reply.copied&&reply.copied->kind) reply.bitmap=decode(reply.copied->image); }
                    if(job.action!=Action::inspect&&job.action!=Action::edit) reply.items=store->list(job.query);
                }catch(winrt::hresult_error const& error) { reply.error=error.code(); }catch(...) { reply.error=E_FAIL; }
                { std::lock_guard lock(state->mutex);if(state->stop) return;if(state->replies.size()>=8) state->replies.pop_front();state->replies.push_back(std::move(reply)); }
                PostMessageW(g_app.window,completed,0,0);
            }
        }catch(...) { state->failed=true;PostMessageW(g_app.window,completed,0,0); }
    });
}
std::wstring query() { wchar_t text[512]{};if(view.search) GetWindowTextW(view.search,text,512);return text; }
void request(Action action,int64_t id=0,DWORD sequence=0) {
    if(action==Action::capture&&service.active) {request_ui(action,id,sequence);return;}
    start_worker();Job job{action,sequence,id,view.query_id,query()};
    { std::lock_guard lock(worker->mutex);if(worker->stop||worker->done) return;
        if(action==Action::capture||action==Action::list) std::erase_if(worker->jobs,[&](Job const& queued){return !queued.ui&&queued.action==action;});
        if(worker->jobs.size()>=16) return;worker->jobs.push_back(std::move(job));
    }worker->condition.notify_one();
}
void request_ui(Action action,int64_t id,DWORD sequence) {
    start_worker();Job job{action,sequence,id,service.query_id,service.query,true,service.selection_id};
    {std::lock_guard lock(worker->mutex);if(worker->stop||worker->done) return;
        if(action==Action::list||action==Action::inspect||action==Action::capture) std::erase_if(worker->jobs,[&](Job const& queued){return queued.ui&&queued.action==action;});
        if(worker->jobs.size()>=16) {service.status=tr(L"操作正在排队，请稍后重试。",L"Operations are queued. Please try again shortly.");service.error=true;++service.revision;return;}
        worker->jobs.push_back(std::move(job));
    }
    if(action==Action::inspect) service.detail_busy=true;else service.busy=true;
    service.error=false;worker->condition.notify_one();
}
void select_ui(int64_t id) {
    ++service.selection_id;service.selected=id;service.detail.reset();service.detail_busy=false;++service.detail_revision;
    if(id) try {request_ui(Action::inspect,id);}catch(...) {service.detail_busy=false;service.error=true;service.status=tr(L"预览暂不可用。",L"Preview is unavailable.");++service.revision;}
}
void status_ui(wchar_t const* value,bool error=false) {service.status=value;service.error=error;++service.revision;}
void snapshot_ui(ClipboardUiState* output) {
    if(!output) return;*output={};output->revision=service.revision;output->detail_revision=service.detail_revision;output->enabled=g_app.settings.clipboard_enabled;output->count=int(service.rows.size());output->busy=service.busy;output->detail_busy=service.detail_busy;output->error=service.error;output->selected=service.selected;
    if(service.detail) {output->detail_id=service.detail->id;output->detail_kind=service.detail->kind;output->text_chars=int(service.detail->text.size());output->image_bytes=int(service.detail->image.size());}
    StringCchCopyW(output->status,240,service.status.c_str());
}
int item_ui(int index,ClipboardUiItem* output) {
    if(!output||index<0||index>=int(service.rows.size())) return 0;*output={};auto const& item=service.rows[size_t(index)];output->id=item.id;output->created=item.created-11644473600000LL;output->kind=item.kind;output->pinned=item.pinned;StringCchCopyW(output->summary,321,item.text.c_str());return 1;
}
void query_ui(wchar_t const* value) {try {service.query=value?std::wstring(value,wcsnlen(value,512)):L"";++service.query_id;request_ui(Action::list);}catch(...) {service.busy=false;status_ui(tr(L"历史数据库暂不可用。",L"History database is unavailable."),true);}}
void action_ui(int action,int64_t id,int value) {
    try {
        if(action==ClipboardEnable) {g_app.settings.clipboard_enabled=value!=0;if(!configure()) g_app.settings.clipboard_enabled=0;save_config();status_ui(g_app.settings.clipboard_enabled?tr(L"正在记录；受保护的内容会跳过。",L"Recording. Protected clipboard content is skipped."):tr(L"自动记录已关闭；已有历史仍可使用。",L"Automatic recording is off. Saved history is still available."));return;}
        if(action==ClipboardCapture) {request_ui(Action::capture,0,GetClipboardSequenceNumber());return;}
        if(action==ClipboardClear) {request_ui(Action::clear);return;}
        if(id<=0) return;
        if(action==ClipboardCopy) request_ui(Action::copy,id);else if(action==ClipboardPinImage) request_ui(Action::preview,id);else if(action==ClipboardTogglePin) request_ui(Action::pin,id);else if(action==ClipboardDelete) request_ui(Action::remove,id);
    }catch(...) {service.busy=false;status_ui(tr(L"操作未完成，请重试。",L"Operation could not finish. Please retry."),true);}
}
int text_ui(wchar_t* output,int capacity) {if(!service.detail) return 0;int length=int(service.detail->text.size());if(output&&capacity>0) {int count=std::min(length,capacity-1);std::copy_n(service.detail->text.data(),count,output);output[count]=0;return count;}return length;}
int image_ui(void* output,int capacity) {if(!service.detail) return 0;int size=int(service.detail->image.size());if(output&&capacity>=size&&size>0) memcpy(output,service.detail->image.data(),size);return size;}
void active_ui(int active) {service.active=active!=0;if(service.active) query_ui(service.query.c_str());else {++service.selection_id;service.detail.reset();service.detail_busy=false;++service.detail_revision;}}
int64_t selected() { int index=ListView_GetNextItem(view.list,-1,LVNI_SELECTED);if(index<0||size_t(index)>=view.rows.size()) return 0;return view.rows[size_t(index)].id; }
void refresh_rows(std::vector<Item> rows) {
    if(!view.window) return;auto previous=selected();view.rows=std::move(rows);SendMessageW(view.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(view.list);
    for(size_t n=0;n<view.rows.size();++n) { auto &row=view.rows[n];std::wstring label=row.text;std::replace(label.begin(),label.end(),L'\n',L' ');std::replace(label.begin(),label.end(),L'\r',L' ');LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(n);item.pszText=label.data();int index=ListView_InsertItem(view.list,&item);ListView_SetItemText(view.list,index,1,const_cast<wchar_t*>(row.kind?tr(L"图片",L"Image"):tr(L"文字",L"Text")));ListView_SetItemText(view.list,index,2,const_cast<wchar_t*>(row.pinned?L"★":L""));if(row.id==previous) ListView_SetItemState(view.list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED); }
    SendMessageW(view.list,WM_SETREDRAW,TRUE,0);InvalidateRect(view.list,nullptr,TRUE);
}
HGLOBAL clipboard_block(void const* bytes,size_t length) {
    auto block=GlobalAlloc(GMEM_MOVEABLE,length);
    if(!block) return nullptr;
    auto data=GlobalLock(block);
    if(!data) { GlobalFree(block);return nullptr; }
    memcpy(data,bytes,length);GlobalUnlock(block);return block;
}
HRESULT publish(Reply& reply) {
    if(!reply.copied||!IsWindow(g_app.window)) return E_FAIL;
    auto& item=*reply.copied;
    unique_gdi<HBITMAP> bitmap;
    HGLOBAL block=nullptr;
    scope_exit free_block([&]{if(block) GlobalFree(block);});
    UINT format=CF_UNICODETEXT;
    if(item.kind) {
        if(!reply.bitmap) return E_FAIL;
        bitmap.reset(static_cast<HBITMAP>(CopyImage(reply.bitmap.get(),IMAGE_BITMAP,0,0,0)));
        if(!bitmap) return E_FAIL;
        format=RegisterClipboardFormatW(L"PNG");
        if(format&&!item.image.empty()) block=clipboard_block(item.image.data(),item.image.size());
    } else {
        block=clipboard_block(item.text.c_str(),(item.text.size()+1)*sizeof(wchar_t));
        if(!block) return E_FAIL;
    }
    // Finish allocations and bitmap conversion before relinquishing the old content.
    if(!OpenClipboard(g_app.window)) return HRESULT_FROM_WIN32(ERROR_BUSY);
    scope_exit close([&]{CloseClipboard();});
    if(!EmptyClipboard()) return E_FAIL;
    bool good=false;
    if(block&&SetClipboardData(format,block)) {block=nullptr;good=true;}
    if(bitmap&&SetClipboardData(CF_BITMAP,bitmap.get())) {bitmap.release();good=true;}
    if(good) view.written=GetClipboardSequenceNumber();
    return good?S_OK:E_FAIL;
}
HWND add_button(HWND parent,wchar_t const* text,int id) { HWND window=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,parent,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);SendMessageW(window,WM_SETFONT,WPARAM(g_app.font),TRUE);return window; }
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        view.window=window;view.search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(10),g_app.instance,nullptr);SendMessageW(view.search,WM_SETFONT,WPARAM(g_app.font),TRUE);SendMessageW(view.search,EM_SETCUEBANNER,TRUE,LPARAM(tr(L"搜索剪贴板历史",L"Search clipboard history")));
        view.enabled=CreateWindowExW(0,L"BUTTON",tr(L"启用本地剪贴板记录",L"Enable local clipboard history"),WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,0,0,1,1,window,reinterpret_cast<HMENU>(11),g_app.instance,nullptr);SendMessageW(view.enabled,BM_SETCHECK,g_app.settings.clipboard_enabled?BST_CHECKED:BST_UNCHECKED,0);
        add_button(window,tr(L"工具首页",L"All tools"),20);add_button(window,tr(L"复制",L"Copy"),21);add_button(window,tr(L"图片贴图",L"Pin image"),22);add_button(window,tr(L"固定／取消固定",L"Pin / unpin"),23);add_button(window,tr(L"删除",L"Delete"),24);add_button(window,tr(L"清空未固定",L"Clear unpinned"),25);
        view.list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,0,0,1,1,window,reinterpret_cast<HMENU>(12),g_app.instance,nullptr);ListView_SetExtendedListViewStyle(view.list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SendMessageW(view.list,WM_SETFONT,WPARAM(g_app.font),TRUE);
        wchar_t const* titles[]={tr(L"内容",L"Content"),tr(L"类型",L"Type"),tr(L"固定",L"Pinned")};for(int n=0;n<3;++n) { LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(titles[n]);column.cx=px(n==0?400:80);ListView_InsertColumn(view.list,n,&column); }
        ListView_SetBkColor(view.list,g_app.dark?RGB(26,33,45):RGB(255,255,255));ListView_SetTextBkColor(view.list,CLR_NONE);ListView_SetTextColor(view.list,g_app.dark?RGB(235,240,249):RGB(29,38,49));view.status=CreateWindowExW(0,L"STATIC",tr(L"勾选后开始记录；只保存在本机。",L"Enable recording to start. History stays on this computer."),WS_CHILD|WS_VISIBLE,0,0,1,1,window,nullptr,g_app.instance,nullptr);SendMessageW(view.status,WM_SETFONT,WPARAM(g_app.small),TRUE);return 0;
    }
    case WM_SIZE: {
        int width=LOWORD(lp),height=HIWORD(lp);MoveWindow(GetDlgItem(window,20),0,0,px(110),px(34),TRUE);MoveWindow(view.search,px(124),0,std::max(px(140),width-px(124)),px(34),TRUE);MoveWindow(view.enabled,px(4),px(51),px(230),px(28),TRUE);MoveWindow(view.status,px(242),px(52),std::max(px(60),width-px(246)),px(35),TRUE);
        MoveWindow(view.list,0,px(99),width,std::max(px(70),height-px(157)),TRUE);ListView_SetColumnWidth(view.list,0,std::max(px(180),width-px(175)));int x=0;for(int id=21;id<=25;++id) { int button_width=id>=23?128:86;MoveWindow(GetDlgItem(window,id),px(x),height-px(43),px(button_width),px(34),TRUE);x+=button_width+8; }return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==10&&HIWORD(wp)==EN_CHANGE) SetTimer(window,1,150,nullptr);
        else if(LOWORD(wp)==11) { g_app.settings.clipboard_enabled=SendMessageW(view.enabled,BM_GETCHECK,0,0)==BST_CHECKED;configure();save_config();SetWindowTextW(view.status,g_app.settings.clipboard_enabled?tr(L"正在记录，密码管理器标记排除的内容不会录入。",L"Recording. Clipboard exclusion flags are respected."):tr(L"记录已关闭；已有历史仍可使用。",L"Recording is off. Saved history is still available.")); }
        else if(LOWORD(wp)==20) { home();PostMessageW(g_app.window,WM_COMMAND,101,0); }
        else if(LOWORD(wp)==25) request(Action::clear);
        else if(auto id=selected()) { if(LOWORD(wp)==21) request(Action::copy,id);else if(LOWORD(wp)==22) request(Action::preview,id);else if(LOWORD(wp)==23) request(Action::pin,id);else if(LOWORD(wp)==24) request(Action::remove,id); }
        return 0;
    case WM_TIMER:KillTimer(window,1);++view.query_id;request(Action::list);return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->code==NM_DBLCLK&&selected()) request(Action::copy,selected());return 0;
    case WM_DRAWITEM: { auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);wchar_t text[100];GetWindowTextW(item->hwndItem,text,100);rounded(item->hDC,item->rcItem,px(10),g_app.dark?RGB(26,33,45):RGB(255,255,255),g_app.dark?RGB(48,59,76):RGB(223,229,237));draw_text(item->hDC,text,item->rcItem,g_app.font,g_app.dark?RGB(235,240,249):RGB(29,38,49),DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE; }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:SetTextColor(HDC(wp),g_app.dark?RGB(235,240,249):RGB(29,38,49));SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));SetBkMode(HDC(wp),TRANSPARENT);return LRESULT(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: { RECT client{};GetClientRect(window,&client);SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));FillRect(HDC(wp),&client,HBRUSH(GetStockObject(DC_BRUSH)));return 1; }
    case WM_DESTROY:if(view.window==window){view.window=view.list=view.search=view.enabled=view.status=nullptr;view.active=false;view.rows.clear();}return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
}
void request_image_editor() {
    try {request(Action::edit,0,GetClipboardSequenceNumber());}
    catch(...) {set_status(L"无法读取剪贴板图片。",L"Could not read the clipboard image.");}
}
bool configure() {
    try {
        if(g_app.settings.clipboard_enabled) { start_worker();if(!view.listening) { view.listening=AddClipboardFormatListener(g_app.window)!=FALSE;if(!view.listening) return false; } }
        else if(view.listening) { RemoveClipboardFormatListener(g_app.window);view.listening=false; }
        settings_sync_clipboard();return true;
    }catch(...) { g_app.settings.clipboard_enabled=0;return false; }
}
bool message(UINT message,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if(message==WM_CLIPBOARDUPDATE) { DWORD sequence=GetClipboardSequenceNumber();if(view.listening&&sequence!=view.written&&sequence!=view.captured) { try { request(Action::capture,0,sequence); }catch(...) { set_status(L"剪贴板记录暂不可用",L"Clipboard recording is unavailable."); } }return true; }
    if(message!=completed) return false;
    if(!worker) return true;if(worker->failed) { g_app.settings.clipboard_enabled=0;configure();save_config();service.busy=service.detail_busy=false;status_ui(tr(L"历史数据库无法打开，未开始记录。",L"History database is unavailable. Recording did not start."),true);if(view.status) SetWindowTextW(view.status,service.status.c_str());return true; }
    std::deque<Reply> replies;{ std::lock_guard lock(worker->mutex);replies=std::move(worker->replies); }
    for(auto &reply:replies) {
        if(reply.action==Action::edit) {
            if(reply.error==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)) set_status(L"图片超过可编辑尺寸。",L"This image exceeds the editing limit.");
            else if(reply.error==HRESULT_FROM_WIN32(ERROR_BUSY)) set_status(L"剪贴板仍被占用，请再试一次。",L"The clipboard is still busy. Please try again.");
            else if(reply.bitmap) capture::edit_clipboard_image(std::move(reply.bitmap));
            else set_status(L"剪贴板中没有可编辑的图片。",L"There is no editable image on the clipboard.");
            continue;
        }
        if(reply.action==Action::capture&&SUCCEEDED(reply.error)) view.captured=reply.sequence;
        if(reply.action==Action::capture&&(reply.error==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)||reply.error==HRESULT_FROM_WIN32(ERROR_BUSY)||reply.error==HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA))) {
            if(reply.ui&&reply.query_id==service.query_id) service.busy=false;
            bool oversized=reply.error==HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            bool busy=reply.error==HRESULT_FROM_WIN32(ERROR_BUSY);
            auto zh=oversized?L"图片超出历史上限（约 3355 万像素或 PNG 16 MiB）。已跳过记录，仍可从系统剪贴板粘贴。":
                busy?L"剪贴板持续被占用，本次未收录。请稍后重试。":L"历史记录空间不足。可取消固定部分记录后重试。";
            auto en=oversized?L"Image exceeds the history limit (33.55 megapixels or 16 MiB PNG). Skipped; pasting is still available.":
                busy?L"Clipboard remained busy. Please retry.":L"History is full. Unpin some entries and retry.";
            status_ui(tr(zh,en),true);set_status(zh,en);
            if(view.status) SetWindowTextW(view.status,tr(zh,en));
            continue;
        }
        if(FAILED(reply.error)) { if(reply.ui) {if(reply.action==Action::inspect&&reply.selection_id==service.selection_id)service.detail_busy=false;else if(reply.query_id==service.query_id)service.busy=false;status_ui(tr(L"该操作未完成；已保存的历史保持不变。",L"Operation could not finish. Saved history is preserved."),true);}if(view.status) SetWindowTextW(view.status,tr(L"该操作未完成；已保存的历史保持不变。",L"Operation could not finish. Saved history is preserved."));continue; }
        if(reply.ui&&reply.action==Action::inspect) {if(reply.selection_id==service.selection_id&&service.active) {service.detail=std::move(reply.copied);service.detail_busy=false;++service.detail_revision;}continue;}
        if(reply.ui&&reply.query_id==service.query_id) {service.rows=std::move(reply.items);service.busy=false;++service.revision;if(service.selected&&std::none_of(service.rows.begin(),service.rows.end(),[](auto const& item){return item.id==service.selected;}))select_ui(0);if(reply.action==Action::capture)status_ui(tr(L"记录已更新；空白、重复或受保护的内容会跳过。",L"History updated. Empty, duplicate or protected content is skipped."));else if(reply.action==Action::remove)status_ui(tr(L"已删除所选记录。",L"Selected entry deleted."));else if(reply.action==Action::clear)status_ui(tr(L"已清空未固定的记录。",L"Unpinned entries cleared."));}
        else if(!reply.ui&&reply.query_id==view.query_id) refresh_rows(std::move(reply.items));
        if(reply.copy) {
            HRESULT result=publish(reply);
            auto message=SUCCEEDED(result)?tr(L"已复制，可以切回应用粘贴。",L"Copied. Switch to an app to paste."):
                result==HRESULT_FROM_WIN32(ERROR_BUSY)?tr(L"剪贴板正在使用，请重试。",L"Clipboard is busy. Please try again."):
                tr(L"未能复制所选记录，请重试。",L"Could not copy this entry. Please try again.");
            if(reply.ui)status_ui(message,FAILED(result));
            if(view.status) SetWindowTextW(view.status,message);
        }
        else if(reply.preview&&reply.bitmap) { BITMAP info{};GetObjectW(reply.bitmap.get(),sizeof info,&info);POINT point;GetCursorPos(&point);capture_pin_bitmap(reply.bitmap.release(),RECT{point.x,point.y,point.x+info.bmWidth,point.y+info.bmHeight},true); }
    }return true;
}
void show() {
    g_app.page=1;view.active=true;
    if(!view.window) { WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.Clipboard";cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,tr(L"剪贴板历史",L"Clipboard history"),WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,g_app.window,nullptr,g_app.instance,nullptr); }
    ++view.query_id;try { request(Action::list); }catch(...) { SetWindowTextW(view.status,tr(L"无法打开历史数据库",L"History database is unavailable")); }layout();
}
void layout() { if(!view.window) return; if(GetParent(view.window)!=g_app.window) { RECT r{};GetClientRect(GetParent(view.window),&r);MoveWindow(view.window,0,0,r.right,r.bottom,TRUE);return; }bool visible=view.active&&g_app.page==1;ShowWindow(view.window,visible?SW_SHOW:SW_HIDE);if(visible) { RECT client{};GetClientRect(g_app.window,&client);MoveWindow(view.window,px(236),px(140),std::max(px(380),int(client.right)-px(270)),std::max(px(220),int(client.bottom)-px(170)),TRUE); } }
void home() { view.active=false;layout(); }
bool shutdown() {
    if(view.listening) { RemoveClipboardFormatListener(g_app.window);view.listening=false; }
    if(worker) { { std::lock_guard lock(worker->mutex);worker->stop=true; }worker->condition.notify_all();if(!worker->done) return false;if(worker->thread.joinable()) worker->thread.join();worker.reset(); }
    if(view.window) DestroyWindow(view.window);return true;
}

HWND embed(HWND parent) { if(!IsWindow(view.window)) view.window=nullptr;show();SetParent(view.window,parent);layout();return view.window; }
ClipboardUiApi const* ui_api() {static ClipboardUiApi api{LB_CLIPBOARD_UI_ABI,sizeof(ClipboardUiApi),snapshot_ui,item_ui,query_ui,select_ui,action_ui,text_ui,image_ui,active_ui};return &api;}
}
