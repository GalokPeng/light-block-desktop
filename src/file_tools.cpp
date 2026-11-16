#include "file_tools.hpp"
#include "rename.hpp"
#include "network.h"
#include "icon_cache.hpp"
#include <thread>
#include <mutex>
#include <optional>
#include <commdlg.h>
namespace lb::files {
namespace {
struct File { std::filesystem::path path;uint64_t size{},modified{};bool directory{}; };
struct Job {
    int action{};uint64_t generation{};std::filesystem::path root,data;std::vector<std::filesystem::path> paths;std::wstring pattern;std::vector<Pair> plan;
    HWND owner{};std::atomic_bool stop{},done{};std::vector<File> entries;std::wstring text;unique_gdi<HBITMAP> image;bool ok{};
};
struct View { HWND window{},folder{},search{},list{},pattern{},text{},image{},status{},apply{};bool visible{};std::filesystem::path root;std::vector<File> entries;std::vector<size_t> shown;std::vector<Pair> plan;unique_gdi<HBITMAP> bitmap; } view;
std::shared_ptr<Job> job;std::thread thread;uint64_t generation{};

// Independent, nonblocking service used by the Qt page. Sessions outlive a
// destroyed page until their worker finishes; mutation jobs are never abandoned.
struct ServiceResult {
    uint64_t serial{},plan_generation{};int action{},kind{},width{},height{};bool ok{},cancelled{},truncated{};
    std::filesystem::path root,path;uint64_t size{},modified{};std::wstring text,error;
    std::vector<File> entries;std::vector<Pair> plan;std::vector<uint32_t> pixels;
};
struct Service {
    std::filesystem::path data;std::thread worker;std::mutex mutex;std::atomic_bool stop{},done{true};bool released{};
    uint64_t next{},plan_generation{},plan_serial{};int action{};ServiceResult result;std::vector<File> entries;std::vector<Pair> plan;
    IconCache icons;bool icons_started{};
};
std::vector<std::shared_ptr<Service>> services;
void reap_services() {
    for(auto it=services.begin();it!=services.end();)if((*it)->released&&(*it)->done){if((*it)->worker.joinable())(*it)->worker.join();it=services.erase(it);}else ++it;
}
void service_finish(Service& service) {
    if(!service.done||!service.worker.joinable())return;service.worker.join();
    if(service.result.ok&&!service.result.cancelled){if(service.result.action==LB_FILES_SCAN)service.entries=std::move(service.result.entries);if(service.result.action==LB_FILES_PLAN&&service.result.plan_generation==service.plan_generation){service.plan=service.result.plan;service.plan_serial=service.result.serial;}}
}
void scan_service(Service& service,ServiceResult& out,size_t limit) {
    WIN32_FIND_DATAW info{};HANDLE handle=FindFirstFileW((out.root/L"*").c_str(),&info);
    if(handle==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;throw std::runtime_error("Cannot open folder");}
    scope_exit close([&]{FindClose(handle);});
    do {if(service.stop){out.cancelled=true;return;}if(info.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_REPARSE_POINT))continue;
        if(!wcscmp(info.cFileName,L".")||!wcscmp(info.cFileName,L".."))continue;
        out.entries.push_back({out.root/info.cFileName,(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow,(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime,(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0});
        if(out.entries.size()>=limit){out.truncated=true;break;}
    }while(FindNextFileW(handle,&info));
    std::sort(out.entries.begin(),out.entries.end(),[](auto const& a,auto const& b){if(a.directory!=b.directory)return a.directory;return StrCmpLogicalW(a.path.filename().c_str(),b.path.filename().c_str())<0;});
}
void preview_service(Service& service,ServiceResult& out) {
    WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(out.path.c_str(),GetFileExInfoStandard,&info))throw std::runtime_error("Cannot read file attributes");
    if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Reparse points are not previewed");
    out.size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;out.modified=(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
    if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){out.kind=LB_FILES_FOLDER;auto original_root=out.root;out.root=out.path;scan_service(service,out,120);out.root=original_root;for(auto const& f:out.entries)out.text+=f.path.filename().wstring()+(f.directory?L" /":L"")+L"\r\n";out.entries.clear();return;}
    if(out.action==LB_FILES_HASH){wchar_t digest[65]{};if(!hash_file(out.path.c_str(),L"SHA256",digest,65,&service.stop))throw std::runtime_error("SHA-256 failed or cancelled");out.kind=LB_FILES_TEXT;out.text=L"SHA-256\r\n"+std::wstring(digest);return;}
    auto kind=media_kind(out.path.c_str());if(kind==MEDIA_IMAGE||kind==MEDIA_GIF){
        unique_gdi<HBITMAP> image(image_thumbnail(out.path.c_str(),1400,1000));if(!image)throw std::runtime_error("Image preview failed");BITMAP bitmap{};if(!GetObjectW(image.get(),sizeof bitmap,&bitmap)||bitmap.bmWidth<1||bitmap.bmHeight<1||bitmap.bmWidth>1400||bitmap.bmHeight>1000)throw std::runtime_error("Invalid preview dimensions");
        out.width=bitmap.bmWidth;out.height=bitmap.bmHeight;out.pixels.resize(size_t(out.width)*out.height);BITMAPINFO dib{};dib.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);dib.bmiHeader.biWidth=out.width;dib.bmiHeader.biHeight=-out.height;dib.bmiHeader.biPlanes=1;dib.bmiHeader.biBitCount=32;dib.bmiHeader.biCompression=BI_RGB;HDC dc=GetDC(nullptr);int rows=GetDIBits(dc,image.get(),0,UINT(out.height),out.pixels.data(),&dib,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);if(rows!=out.height)throw std::runtime_error("Image preview transfer failed");for(auto& pixel:out.pixels)pixel|=0xff000000;out.kind=LB_FILES_IMAGE;return;
    }
    auto ext=out.path.extension().wstring();CharLowerBuffW(ext.data(),DWORD(ext.size()));std::wstring known=L"|.txt|.md|.json|.csv|.py|.log|.yaml|.yml|.ini|.xml|.html|.css|.js|.ts|.cpp|.hpp|.c|.h|";
    if(ext.empty()||known.find(L"|"+ext+L"|")==std::wstring::npos){out.kind=LB_FILES_UNSUPPORTED;return;}
    unique_handle file(CreateFileW(out.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file)throw std::runtime_error("Cannot read file");std::string bytes(512*1024,'\0');DWORD count{};if(!ReadFile(file.get(),bytes.data(),DWORD(bytes.size()),&count,nullptr))throw std::runtime_error("Cannot read file");bytes.resize(count);out.truncated=out.size>count;
    if(bytes.starts_with("\xff\xfe")){for(size_t n=2;n+1<bytes.size();n+=2)out.text+=wchar_t(BYTE(bytes[n])|(unsigned(BYTE(bytes[n+1]))<<8));}
    else if(bytes.starts_with("\xfe\xff")){for(size_t n=2;n+1<bytes.size();n+=2)out.text+=wchar_t((unsigned(BYTE(bytes[n]))<<8)|BYTE(bytes[n+1]));}
    else {if(bytes.starts_with("\xef\xbb\xbf"))bytes.erase(0,3);if(bytes.find('\0')!=std::string::npos){out.kind=LB_FILES_UNSUPPORTED;return;}int count=MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),nullptr,0);out.text.resize(size_t(count));if(count)MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),out.text.data(),count);}
    out.kind=LB_FILES_TEXT;
}
void* service_create(wchar_t const* directory) {
    try{if(!directory||!*directory||wcslen(directory)>=LB_FILES_PATH)return nullptr;reap_services();auto service=std::make_shared<Service>();service->data=directory;services.push_back(service);return service.get();}catch(...){return nullptr;}
}
void service_release(void* handle){if(!handle)return;auto& s=*static_cast<Service*>(handle);s.released=true;s.icons.stop(0);if(s.action<=LB_FILES_PLAN)s.stop=true;reap_services();}
void service_cancel(void* handle){if(handle){auto& s=*static_cast<Service*>(handle);if(s.action<=LB_FILES_PLAN)s.stop=true;}}
void service_invalidate(void* handle){if(handle){auto& s=*static_cast<Service*>(handle);++s.plan_generation;s.plan.clear();s.plan_serial=0;}}
uint64_t service_start(void* handle,FileToolsRequest const* request) {
    if(!handle||!request)return 0;auto& s=*static_cast<Service*>(handle);if(s.released||!s.done||request->action<0||request->action>LB_FILES_RECOVER||request->count<0||request->count>1000)return 0;
    if((request->root&&wcslen(request->root)>=LB_FILES_PATH)||(request->pattern&&wcslen(request->pattern)>256)||(request->action==LB_FILES_SCAN&&(!request->root||!*request->root)))return 0;
    if(request->action>=LB_FILES_APPLY){if(job&&!job->done&&job->action>=4)return 0;for(auto const& other:services)if(other.get()!=&s&&!other->done&&other->action>=LB_FILES_APPLY)return 0;}
    service_finish(s);try {
        auto result=std::make_shared<ServiceResult>();result->serial=++s.next;result->action=request->action;result->plan_generation=s.plan_generation;result->root=request->root?request->root:L"";
        std::vector<std::filesystem::path> paths;for(int n=0;n<request->count;++n){if(!request->paths||!request->paths[n]||wcslen(request->paths[n])>=LB_FILES_PATH)return 0;paths.emplace_back(request->paths[n]);}
        std::wstring pattern=request->pattern?request->pattern:L"";std::vector<Pair> plan;
        if(request->action==LB_FILES_APPLY){if(s.plan.empty()||request->plan_serial!=s.plan_serial)return 0;plan=s.plan;service_invalidate(handle);}
        if(request->action==LB_FILES_SCAN||request->action==LB_FILES_UNDO||request->action==LB_FILES_RECOVER)service_invalidate(handle);
        if(request->action==LB_FILES_PREVIEW||request->action==LB_FILES_HASH){if(paths.size()!=1)return 0;result->path=paths[0];}
        s.action=request->action;s.stop=false;s.done=false;
        try{s.worker=std::thread([&s,result,paths=std::move(paths),pattern=std::move(pattern),plan=std::move(plan)]{
            com_apartment apartment(COINIT_MULTITHREADED);scope_exit done([&]{std::lock_guard lock(s.mutex);s.result=std::move(*result);s.done=true;});
            try {if(result->action==LB_FILES_SCAN)scan_service(s,*result,10000);else if(result->action==LB_FILES_PREVIEW||result->action==LB_FILES_HASH)preview_service(s,*result);
                else if(result->action==LB_FILES_PLAN)result->plan=preview(paths,pattern);
                else {Journal journal(s.data/L"file-journal.db");if(result->action==LB_FILES_APPLY)journal.apply(plan);else if(result->action==LB_FILES_UNDO)journal.undo();else journal.recover();}
                result->cancelled=result->cancelled||(s.stop&&result->action<=LB_FILES_PLAN);result->ok=!result->cancelled;
            }catch(std::exception const& error){result->cancelled=s.stop&&result->action<=LB_FILES_PLAN;try{result->error=sql::wide(error.what());}catch(...){result->error=L"File operation failed";}}catch(...){result->error=L"File operation failed";}
        });}catch(...){s.done=true;throw;}
        return result->serial;
    }catch(...){return 0;}
}
int service_poll(void* handle,FileToolsStatus* out){if(!handle||!out)return 0;auto& s=*static_cast<Service*>(handle);service_finish(s);std::lock_guard lock(s.mutex);*out={};auto const& r=s.result;out->serial=r.serial;out->busy=!s.done;out->action=s.done?r.action:s.action;out->ok=r.ok;out->cancelled=r.cancelled;out->count=int(s.entries.size());out->plan_serial=s.plan_serial;out->plan_count=int(s.plan.size());out->kind=r.kind;out->width=r.width;out->height=r.height;out->truncated=r.truncated;out->size=r.size;out->modified=r.modified;StringCchCopyW(out->root,LB_FILES_PATH,r.root.c_str());StringCchCopyW(out->path,LB_FILES_PATH,r.path.c_str());StringCchCopyW(out->message,2048,r.error.c_str());return 1;}
int service_entry(void* handle,int index,FileToolsEntry* out){if(!handle||!out)return 0;auto& entries=static_cast<Service*>(handle)->entries;if(index<0||size_t(index)>=entries.size())return 0;auto const& f=entries[size_t(index)];*out={};out->size=f.size;out->modified=f.modified;out->directory=f.directory;StringCchCopyW(out->path,LB_FILES_PATH,f.path.c_str());StringCchCopyW(out->name,512,f.path.filename().c_str());return 1;}
int service_icon(void* handle,int index,wchar_t const* expected,uint64_t modified,uint32_t* output,int capacity){
    constexpr int side=LB_FILES_ICON_SIZE,count=side*side;
    if(!handle||!expected||!output||capacity<count)return -1;
    auto& s=*static_cast<Service*>(handle);if(s.released||index<0||size_t(index)>=s.entries.size())return -1;
    auto const& entry=s.entries[size_t(index)];if(entry.path.native()!=expected||entry.modified!=modified)return -1;
    try{
        if(!s.icons_started){s.icons.start(nullptr,0,48,true);s.icons_started=true;}
        s.icons.receive();auto path=entry.path.native();auto icon=s.icons.get(path+L"\n"+std::to_wstring(modified),path,entry.directory,false);if(!icon)return 0;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=side;info.bmiHeader.biHeight=-side;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* pixels=nullptr;unique_gdi<HBITMAP> bitmap(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0));if(!bitmap)return -1;
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return -1;scope_exit close([&]{DeleteDC(dc);});auto previous=SelectObject(dc,bitmap.get());scope_exit restore([&]{SelectObject(dc,previous);});
        auto raw=static_cast<uint32_t*>(pixels);std::fill_n(raw,count,0u);if(!DrawIconEx(dc,0,0,icon,side,side,0,nullptr,DI_NORMAL))return -1;
        GdiFlush();std::copy_n(raw,count,output);return count;
    }catch(...){return -1;}
}
int service_pair(void* handle,int index,FileToolsPair* out){if(!handle||!out)return 0;auto& plan=static_cast<Service*>(handle)->plan;if(index<0||size_t(index)>=plan.size())return 0;*out={};StringCchCopyW(out->source,LB_FILES_PATH,plan[size_t(index)].source.c_str());StringCchCopyW(out->target,LB_FILES_PATH,plan[size_t(index)].target.c_str());return 1;}
int service_text(void* handle,wchar_t* output,int capacity){if(!handle)return 0;auto& s=*static_cast<Service*>(handle);std::lock_guard lock(s.mutex);int count=int(s.result.text.size());if(output&&capacity>0){int copy=std::min(count,capacity-1);std::copy_n(s.result.text.data(),copy,output);output[copy]=0;}return count;}
int service_pixels(void* handle,uint32_t* output,int capacity){if(!handle)return 0;auto& s=*static_cast<Service*>(handle);std::lock_guard lock(s.mutex);int count=int(s.result.pixels.size());if(output&&capacity>=count)std::copy(s.result.pixels.begin(),s.result.pixels.end(),output);return count;}
bool service_shutdown(){bool ready=true;for(auto const& s:services){if(s->action<=LB_FILES_PLAN)s->stop=true;if(!s->done)ready=false;else service_finish(*s);}reap_services();return ready;}

int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
HWND control(wchar_t const* cls,wchar_t const* label,DWORD style,int id) { auto h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,view.window,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);SendMessageW(h,WM_SETFONT,WPARAM(g_app.font),TRUE);return h; }
std::wstring text(HWND h) { int size=GetWindowTextLengthW(h);std::wstring result(size+1,L'\0');GetWindowTextW(h,result.data(),size+1);result.resize(size);return result; }
std::vector<std::filesystem::path> selected() { std::vector<std::filesystem::path> result;for(int n=-1;(n=ListView_GetNextItem(view.list,n,LVNI_SELECTED))>=0;) if(size_t(n)<view.shown.size()) result.push_back(view.entries[view.shown[size_t(n)]].path);return result; }
void refresh() {
    if(!view.window) return;auto query=text(view.search);view.shown.clear();SendMessageW(view.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(view.list);
    for(size_t n=0;n<view.entries.size();++n) { auto const& file=view.entries[n];auto name=file.path.filename().wstring();if(!query.empty()&&!StrStrIW(name.c_str(),query.c_str())) continue;LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(view.shown.size());item.pszText=name.data();int row=ListView_InsertItem(view.list,&item);auto size=file.directory?std::wstring(tr(L"文件夹",L"Folder")):std::to_wstring(file.size)+L" B";ListView_SetItemText(view.list,row,1,size.data());view.shown.push_back(n); }
    SendMessageW(view.list,WM_SETREDRAW,TRUE,0);InvalidateRect(view.list,nullptr,TRUE);view.plan.clear();EnableWindow(view.apply,FALSE);
}
void run(int action,std::vector<std::filesystem::path> paths={}) {
    if(action>=4)for(auto const& service:services)if(!service->done&&service->action>=LB_FILES_APPLY){SetWindowTextW(view.status,tr(L"后台文件操作尚未完成。",L"A file operation is still running."));return;}
    if(job&&!job->done) { SetWindowTextW(view.status,tr(L"后台任务尚未完成。",L"A background task is still running."));return; }
    if(thread.joinable()) thread.join();auto state=std::make_shared<Job>();state->action=action;state->generation=generation;state->root=view.root;state->data=g_app.data;state->paths=std::move(paths);state->pattern=text(view.pattern);state->plan=view.plan;state->owner=g_app.window;job=state;for(int id:{1,2,5,6,7,10,11,12,13,14,15}) EnableWindow(GetDlgItem(view.window,id),FALSE);SetWindowTextW(view.status,tr(L"正在后台处理…",L"Working in the background…"));
    thread=std::thread([state] {
        com_apartment apartment(COINIT_MULTITHREADED);scope_exit done([&]{state->done=true;PostMessageW(state->owner,WM_APP+138,0,0);});
        try {
            if(state->action==0) {
                WIN32_FIND_DATAW info{};HANDLE handle=FindFirstFileW((state->root/L"*").c_str(),&info);if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open folder");scope_exit close([&]{FindClose(handle);});
                do { if(state->stop) return;if(info.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_REPARSE_POINT)) continue;if(!wcscmp(info.cFileName,L".")||!wcscmp(info.cFileName,L"..")) continue;state->entries.push_back({state->root/info.cFileName,(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow,(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime,(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0});if(state->entries.size()>=10000) break; }while(FindNextFileW(handle,&info));
                std::sort(state->entries.begin(),state->entries.end(),[](auto const& a,auto const& b){if(a.directory!=b.directory) return a.directory;return StrCmpLogicalW(a.path.filename().c_str(),b.path.filename().c_str())<0;});
            }else if(state->action==1||state->action==2) {
                if(state->paths.size()!=1) throw std::runtime_error("Select one file");auto const& path=state->paths.front();if(GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_DIRECTORY) throw std::runtime_error("Select a file");
                state->text=path.filename().wstring()+L"\r\n";
                if(state->action==2) { wchar_t digest[65];if(!hash_file(path.c_str(),L"SHA256",digest,65,&state->stop)) throw std::runtime_error("Hash failed or cancelled");state->text+=L"SHA-256\r\n"+std::wstring(digest); }
                else if(media_kind(path.c_str())==MEDIA_IMAGE||media_kind(path.c_str())==MEDIA_GIF) { state->image.reset(image_thumbnail(path.c_str(),560,300));if(!state->image) throw std::runtime_error("Image preview failed"); }
                else {
                    unique_handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!file) throw std::runtime_error("Cannot read file");std::string bytes(65536,'\0');DWORD count=0;if(!ReadFile(file.get(),bytes.data(),DWORD(bytes.size()),&count,nullptr)) throw std::runtime_error("Cannot read file");bytes.resize(count);
                    if(bytes.size()>=2&&BYTE(bytes[0])==0xff&&BYTE(bytes[1])==0xfe) { std::wstring contents;for(size_t i=2;i+1<bytes.size();i+=2) contents+=wchar_t(BYTE(bytes[i])|(unsigned(BYTE(bytes[i+1]))<<8));state->text+=contents; }
                    else { if(bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0,3);if(bytes.find('\0')!=std::string::npos) state->text+=L"[Binary file]";else state->text+=sql::wide(bytes); }
                }
            }else if(state->action==3) { state->plan=preview(state->paths,state->pattern);for(auto const& p:state->plan) state->text+=p.source.filename().wstring()+L"  →  "+p.target.filename().wstring()+L"\r\n"; }
            else { Journal journal(state->data/L"file-journal.db");if(state->action==4) journal.apply(state->plan);else if(state->action==5) journal.undo();else journal.recover(); }
            state->ok=true;
        }catch(std::exception const& error) { try { state->text=sql::wide(error.what()); }catch(...) { state->text=L"File operation failed"; } }catch(...) { state->text=L"File operation failed"; }
    });
}
void choose() {
    winrt::com_ptr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put())))) return;DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);if(FAILED(dialog->Show(g_app.window))) return;winrt::com_ptr<IShellItem> item;if(FAILED(dialog->GetResult(item.put()))) return;PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { view.root=path;CoTaskMemFree(path);SetWindowTextW(view.folder,view.root.c_str());run(0); }
}
void position() {
    RECT r{};GetClientRect(view.window,&r);int w=r.right,h=r.bottom;
    MoveWindow(view.folder,0,0,std::max(px(80),w-px(240)),px(32),TRUE);MoveWindow(GetDlgItem(view.window,1),w-px(230),0,px(100),px(32),TRUE);MoveWindow(GetDlgItem(view.window,2),w-px(120),0,px(54),px(32),TRUE);MoveWindow(GetDlgItem(view.window,3),w-px(56),0,px(56),px(32),TRUE);
    MoveWindow(view.search,0,px(43),w/2-px(10),px(32),TRUE);MoveWindow(view.pattern,w/2,px(43),w/2,px(32),TRUE);
    int button=(w-px(45))/6;for(int n=0;n<6;++n) MoveWindow(GetDlgItem(view.window,10+n),n*(button+px(9)),px(86),button,px(32),TRUE);
    int content=std::max(px(90),h-px(167));MoveWindow(view.list,0,px(130),w/2-px(10),content,TRUE);ListView_SetColumnWidth(view.list,0,std::max(px(100),w/2-px(130)));MoveWindow(view.text,w/2,px(130),w/2,content,TRUE);MoveWindow(view.image,w/2,px(130),w/2,content,TRUE);MoveWindow(view.status,0,h-px(30),w,px(30),TRUE);
}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        ++generation;view.window=window;view.folder=control(L"EDIT",L"",WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,4);control(L"BUTTON",tr(L"选择目录",L"Folder"),WS_TABSTOP,1);control(L"BUTTON",L"↑",WS_TABSTOP,2);control(L"BUTTON",L"←",WS_TABSTOP,3);
        view.search=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,5);SendMessageW(view.search,EM_SETCUEBANNER,TRUE,LPARAM(tr(L"搜索当前目录",L"Search this folder")));view.pattern=control(L"EDIT",L"{name}-{n:03}{ext}",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,6);
        wchar_t const* labels[]={tr(L"预览",L"Preview"),L"SHA-256",tr(L"改名预览",L"Plan rename"),tr(L"应用改名",L"Apply"),tr(L"撤销",L"Undo"),tr(L"恢复中断",L"Recover")};for(int n=0;n<6;++n) { auto h=control(L"BUTTON",labels[n],WS_TABSTOP,10+n);if(n==3) view.apply=h; }EnableWindow(view.apply,FALSE);
        view.list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS,7);ListView_SetExtendedListViewStyle(view.list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);for(int n=0;n<2;++n) { LVCOLUMNW column{};column.mask=LVCF_WIDTH|LVCF_TEXT;column.cx=px(n?100:220);column.pszText=const_cast<PWSTR>(n?tr(L"大小",L"Size"):tr(L"名称",L"Name"));ListView_InsertColumn(view.list,n,&column); }
        view.text=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,8);view.image=control(L"STATIC",L"",SS_BITMAP|SS_CENTERIMAGE,9);ShowWindow(view.image,SW_HIDE);view.status=control(L"STATIC",L"",0,20);return 0;
    }
    case WM_DESTROY:
        // Qt may destroy/rebuild the embedding root while a worker is still running.
        // Its completion must never target recycled control handles or a new view.
        if(view.window==window) {
            ++generation;if(job&&job->action<=3) job->stop=true;
            SendMessageW(view.image,STM_SETIMAGE,IMAGE_BITMAP,0);view.bitmap.reset();
            view.window=view.folder=view.search=view.list=view.pattern=view.text=view.image=view.status=view.apply=nullptr;
            view.visible=false;view.entries.clear();view.shown.clear();view.plan.clear();
        }return 0;
    case WM_SIZE:position();return 0;
    case WM_COMMAND: {
        int id=LOWORD(wp);if(id==1) choose();else if(id==2) { auto parent=view.root.parent_path();if(!parent.empty()) { view.root=parent;SetWindowTextW(view.folder,parent.c_str());run(0); } }else if(id==3) home();else if(id==5&&HIWORD(wp)==EN_CHANGE) refresh();else if(id==6&&HIWORD(wp)==EN_CHANGE) { view.plan.clear();EnableWindow(view.apply,FALSE); }
        else if(id==10||id==11) run(id-9,selected());else if(id==12) run(3,selected());else if(id==13&&!view.plan.empty()) { if(MessageBoxW(g_app.window,tr(L"按右侧预览重命名这些文件？可撤销，原文件内容保持不变。",L"Rename these files using the preview? The change can be undone."),tr(L"确认重命名",L"Confirm rename"),MB_YESNO|MB_ICONQUESTION)==IDYES) run(4); }else if(id==14) run(5);else if(id==15) run(6);return 0;
    }
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->code==NM_DBLCLK) { auto paths=selected();if(paths.size()==1) { if(GetFileAttributesW(paths[0].c_str())&FILE_ATTRIBUTE_DIRECTORY) { view.root=paths[0];SetWindowTextW(view.folder,view.root.c_str());run(0); }else run(1,std::move(paths)); } }return 0;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(HDC(wp),g_app.dark?RGB(235,240,249):RGB(29,38,49));SetBkColor(HDC(wp),g_app.dark?RGB(26,33,45):RGB(255,255,255));SetDCBrushColor(HDC(wp),g_app.dark?RGB(26,33,45):RGB(255,255,255));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: { RECT r{};GetClientRect(window,&r);SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));FillRect(HDC(wp),&r,HBRUSH(GetStockObject(DC_BRUSH)));return 1; }
    }return DefWindowProcW(window,message,wp,lp);
}
}
void show() {
    view.visible=true;g_app.page=1;
    if(!view.window) { WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.FileWorkbench";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,g_app.window,nullptr,g_app.instance,nullptr);PWSTR path=nullptr;if(view.root.empty()) { if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&path))) { view.root=path;CoTaskMemFree(path); }else view.root=g_app.data; }SetWindowTextW(view.folder,view.root.c_str());run(0); }
    layout();InvalidateRect(g_app.window,nullptr,FALSE);
}
void home() { view.visible=false;layout();InvalidateRect(g_app.window,nullptr,FALSE); }
void layout() { if(!view.window) return; if(GetParent(view.window)!=g_app.window) { RECT r{};GetClientRect(GetParent(view.window),&r);MoveWindow(view.window,0,0,r.right,r.bottom,TRUE);return; }bool visible=view.visible&&g_app.page==1;ShowWindow(view.window,visible?SW_SHOW:SW_HIDE);if(visible) { RECT r{};GetClientRect(g_app.window,&r);MoveWindow(view.window,px(236),px(137),std::max(px(340),int(r.right)-px(270)),std::max(px(180),int(r.bottom)-px(159)),TRUE); } }
bool message(UINT message,WPARAM,LPARAM) {
    if(message!=WM_APP+138) return false;if(!job||!job->done) return true;if(thread.joinable()) thread.join();auto finished=std::exchange(job,{});if(!view.window) return true;if(finished->generation!=generation) { run(0);return true; }for(int id:{1,2,5,6,7,10,11,12,14,15}) EnableWindow(GetDlgItem(view.window,id),TRUE);
    if(finished->ok&&finished->action==0) { view.entries=std::move(finished->entries);refresh();SetWindowTextW(view.status,tr(L"按住 Ctrl 或 Shift 多选文件；先预览改名，再应用。",L"Ctrl/Shift selects files. Preview a rename before applying.")); }
    else if(finished->ok&&finished->action>=4) { view.plan.clear();SetWindowTextW(view.status,tr(L"操作已完成，恢复记录已保存。",L"Operation completed. Recovery journal saved."));run(0); }
    else {
        SetWindowTextW(view.text,finished->text.c_str());SendMessageW(view.image,STM_SETIMAGE,IMAGE_BITMAP,0);view.bitmap=std::move(finished->image);if(view.bitmap) SendMessageW(view.image,STM_SETIMAGE,IMAGE_BITMAP,LPARAM(view.bitmap.get()));ShowWindow(view.text,view.bitmap?SW_HIDE:SW_SHOW);ShowWindow(view.image,view.bitmap?SW_SHOW:SW_HIDE);
        if(finished->ok&&finished->action==3) { view.plan=std::move(finished->plan);EnableWindow(view.apply,TRUE); }SetWindowTextW(view.status,finished->ok?tr(L"处理完成",L"Ready"):tr(L"操作未完成；详情见右侧，文件保持可恢复状态。",L"Operation failed. See details; recovery records are preserved."));
    }return true;
}
bool shutdown() { if(!service_shutdown())return false;if(job&&!job->done) { job->stop=true;return false; }if(thread.joinable()) thread.join();job.reset();if(view.window) DestroyWindow(view.window);view.window=nullptr;view.bitmap.reset();return true; }

bool lifecycle_tests() {
    if(view.window||job||thread.joinable()) return false;
    auto original_window=g_app.window;auto original_page=g_app.page;auto original_dpi=g_app.dpi;g_app.dpi=96;INITCOMMONCONTROLSEX controls{sizeof controls,ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);auto root=std::filesystem::path(g_app.data)/L"file-view-lifecycle";
    auto first=root/L"first",second=root/L"second";std::filesystem::create_directories(first);std::filesystem::create_directories(second);
    write_utf8_atomic((first/L"old.txt").c_str(),L"old");write_utf8_atomic((second/L"new.txt").c_str(),L"new");
    HWND owner=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,600,400,nullptr,nullptr,g_app.instance,nullptr);if(!owner) { g_app.dpi=original_dpi;return false; }
    scope_exit cleanup([&]{if(job) job->stop=true;if(thread.joinable()) thread.join();shutdown();DestroyWindow(owner);g_app.window=original_window;g_app.page=original_page;g_app.dpi=original_dpi;view.root.clear();std::error_code ignored;std::filesystem::remove(first/L"old.txt",ignored);std::filesystem::remove(second/L"new.txt",ignored);std::filesystem::remove(first,ignored);std::filesystem::remove(second,ignored);std::filesystem::remove(root,ignored);});
    g_app.window=owner;view.root=first;show();if(!view.window) return false;DestroyWindow(view.window);
    if(view.window||view.list||view.status) return false;view.root=second;show();if(!view.window) return false;
    auto deadline=GetTickCount64()+5000;while(job&&GetTickCount64()<deadline) { if(job->done) message(WM_APP+138,0,0);else Sleep(1); }
    return !job&&IsWindow(view.list)&&view.entries.size()==1&&view.entries.front().path.filename()==L"new.txt";
}
HWND embed(HWND parent) { if(!IsWindow(view.window)) view.window=nullptr;show();SetParent(view.window,parent);layout();return view.window; }
FileToolsUiApi const* ui_api(){static FileToolsUiApi const api{LB_FILES_ABI,sizeof(FileToolsUiApi),service_create,service_release,service_start,service_cancel,service_poll,service_entry,service_pair,service_text,service_pixels,service_invalidate,service_icon};return &api;}

bool ui_service_tests(std::filesystem::path const& root) {
    try {
        auto directory=root/(L"file-service-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory/L"folder");auto a=directory/L"a.txt",b=directory/L"b.txt";
        if(!write_utf8_atomic(a.c_str(),L"abc")||!write_utf8_atomic(b.c_str(),L"原位预览"))return false;
        auto handle=service_create(directory.c_str());if(!handle)return false;scope_exit release([&]{service_release(handle);});
        auto wait=[&]{FileToolsStatus status{};auto deadline=GetTickCount64()+10000;do{service_poll(handle,&status);if(!status.busy)return status;Sleep(1);}while(GetTickCount64()<deadline);return status;};
        auto run=[&](int action,std::vector<std::filesystem::path> const& paths={},wchar_t const* pattern=L"",uint64_t serial=0){std::vector<wchar_t const*> values;for(auto const& path:paths)values.push_back(path.c_str());FileToolsRequest request{action,int(values.size()),directory.c_str(),values.data(),pattern,serial};return service_start(handle,&request)!=0;};
        if(!run(LB_FILES_SCAN))return false;auto status=wait();if(status.busy||!status.ok||status.count!=3)return false;
        // Folder and extension association icons are resolved on the shell worker.
        // Reject stale identity, insufficient buffers and invalid indices before IO.
        std::array<uint32_t,LB_FILES_ICON_SIZE*LB_FILES_ICON_SIZE> icon_pixels{};
        for(int index=0;index<3;++index){FileToolsEntry entry{};if(!service_entry(handle,index,&entry))return false;int received=0;auto icon_deadline=GetTickCount64()+10000;
            while(!(received=service_icon(handle,index,entry.path,entry.modified,icon_pixels.data(),int(icon_pixels.size())))&&GetTickCount64()<icon_deadline)Sleep(1);
            if(received!=int(icon_pixels.size())||std::none_of(icon_pixels.begin(),icon_pixels.end(),[](uint32_t p){return (p>>24)!=0;}))return false;
            if(service_icon(handle,index,L"different-path",entry.modified,icon_pixels.data(),int(icon_pixels.size()))!=-1||service_icon(handle,index,entry.path,entry.modified+1,icon_pixels.data(),int(icon_pixels.size()))!=-1||service_icon(handle,index,entry.path,entry.modified,icon_pixels.data(),1)!=-1)return false;
        }
        if(service_icon(handle,99,L"invalid",0,icon_pixels.data(),int(icon_pixels.size()))!=-1)return false;
        if(!run(LB_FILES_PREVIEW,{directory/L"folder"}))return false;status=wait();if(!status.ok||status.kind!=LB_FILES_FOLDER)return false;
        if(!run(LB_FILES_PREVIEW,{b}))return false;status=wait();wchar_t contents[256]{};service_text(handle,contents,256);if(!status.ok||std::wstring(contents)!=L"原位预览")return false;
        if(!run(LB_FILES_HASH,{a}))return false;status=wait();service_text(handle,contents,256);if(!status.ok||std::wstring(contents)!=L"SHA-256\r\nba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")return false;
        if(!run(LB_FILES_PLAN,{a,b},L"renamed-{n:03}{ext}"))return false;status=wait();if(!status.ok||status.plan_count!=2||!status.plan_serial)return false;auto stale=status.plan_serial;service_invalidate(handle);if(run(LB_FILES_APPLY,{},L"",stale))return false;
        if(!run(LB_FILES_PLAN,{a,b},L"renamed-{n:03}{ext}"))return false;status=wait();if(!status.ok||!run(LB_FILES_APPLY,{},L"",status.plan_serial))return false;status=wait();if(!status.ok||std::filesystem::exists(a)||!std::filesystem::exists(directory/L"renamed-001.txt"))return false;
        if(!run(LB_FILES_UNDO))return false;status=wait();if(!status.ok||!std::filesystem::exists(a)||!std::filesystem::exists(b))return false;
        {Journal journal(directory/L"file-journal.db");journal.apply(preview({a,b},L"interrupted-{n}{ext}"),0,1);if(journal.pending().empty())return false;}
        if(!run(LB_FILES_RECOVER))return false;status=wait();if(!status.ok||!std::filesystem::exists(a)||!std::filesystem::exists(b))return false;
        // Invalidating while plan is still running cannot restore an apply token.
        if(!run(LB_FILES_PLAN,{a,b},L"late-{n}{ext}"))return false;service_invalidate(handle);status=wait();if(status.plan_serial||status.plan_count)return false;
        // Release a running session without waiting on the UI thread.
        size_t initial=services.size();void* temporary=service_create(directory.c_str());FileToolsRequest scan{LB_FILES_SCAN,0,directory.c_str(),nullptr,L"",0};if(!service_start(temporary,&scan))return false;service_release(temporary);auto deadline=GetTickCount64()+5000;while(services.size()>initial&&GetTickCount64()<deadline){reap_services();Sleep(1);}return services.size()==initial;
    }catch(...){return false;}
}

}
