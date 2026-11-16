#include "lb.h"
#include "json.h"
#include "network.h"
#include "ui_api.h"
#include "online_catalog_ui_api.h"
#include <set>
#include <filesystem>

#define WM_ONLINE_DONE (WM_APP+75)
#define WM_ONLINE_PROGRESS (WM_APP+76)
struct OnlineItem { wchar_t id[100],title[180],url[4096],thumbnail[4096],filename[180],license[100],author[300],source[4096],sha1[41],local[LB_PATH];MediaKind kind;uint64_t bytes; };
static struct { HWND window,list,filter,search,details,progress,download,preview;HBRUSH brush;OnlineItem *items;UINT count,capacity;HANDLE thread;volatile LONG cancel;UINT job;BOOL preview_job;wchar_t job_path[LB_PATH];HRESULT result;int percent;uint32_t serial;std::atomic<uint64_t> received{},total{},activity{};std::atomic<int> stage{};uint64_t started{}; } online;
static void prune_previews(uintmax_t budget=128*1024*1024){
    try{
        auto root=std::filesystem::path(g_app.data)/L"previews";auto attrs=GetFileAttributesW(root.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return;
        struct Cached{std::filesystem::path file;std::filesystem::file_time_type time;uintmax_t bytes;};std::vector<Cached> files;uintmax_t total=0;
        for(auto const& entry:std::filesystem::directory_iterator(root)){
            if(!entry.is_regular_file()||entry.is_symlink()||entry.path().extension()==L".verified"||entry.path().extension()==L".partial"||!std::filesystem::is_regular_file(entry.path().wstring()+L".verified"))continue;
            auto size=entry.file_size();total+=size;files.push_back({entry.path(),entry.last_write_time(),size});
        }
        std::sort(files.begin(),files.end(),[](auto const& a,auto const& b){return a.time<b.time;});
        for(auto const& file:files){if(total<=budget)break;if(player_uses_file(file.file.c_str())||!_wcsicmp(file.file.c_str(),online.job_path))continue;if(DeleteFileW(file.file.c_str())){DeleteFileW((file.file.wstring()+L".verified").c_str());total-=file.bytes;}}
    }catch(...){/* Cache cleanup must not interrupt downloading or playback. */}
}
static void report_download_result() {
    prune_previews();
    if(SUCCEEDED(online.result)) {
        if(online.preview_job) set_status(L"预览文件已就绪，校验通过。",L"Preview file is ready and verified.");
        else set_status(L"壁纸下载并校验完成，可以应用到桌面。",L"Wallpaper downloaded and verified. You can apply it to your desktop.");
        return;
    }
    log_line(L"Online wallpaper download ended: id=%s preview=%d HRESULT=0x%08lX",online.items&&online.job<online.count?online.items[online.job].id:L"unknown",int(online.preview_job),static_cast<unsigned long>(online.result));
    if(online.result==HRESULT_FROM_WIN32(ERROR_CANCELLED)) set_status(L"已取消下载。",L"Download cancelled.");
    else set_status(L"在线壁纸未能下载或校验未通过，可以重试。",L"Wallpaper download failed or validation did not match. Try again.");
}
static BOOL get_string(const Json *j,int object,const char *key,wchar_t *value,size_t capacity) { return json_string(j,json_property(j,object,key),value,capacity); }
static std::wstring receipt(OnlineItem const& item,wchar_t const* path=nullptr) {
    WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(path?path:item.local,GetFileExInfoStandard,&info)) return {};
    uint64_t size=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;if(size!=item.bytes) return {};
    return std::wstring(item.sha1)+L"\n"+std::to_wstring(size)+L"\n"+std::to_wstring((uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime)+L"\n";
}
static BOOL downloaded(OnlineItem const& item,wchar_t const* file=nullptr) {
    auto expected=receipt(item,file);if(expected.empty()) return FALSE;std::wstring path=std::wstring(file?file:item.local)+L".verified";
    std::unique_ptr<wchar_t,decltype(&free)> actual(read_utf8_file(path.c_str(),512),free);return actual&&expected==actual.get();
}
static BOOL catalog_host(const wchar_t *url) {
    if(!safe_https_url(url)) return FALSE;
    const wchar_t *host=url+8,*end=wcschr(host,L'/');size_t length=end?(size_t)(end-host):wcslen(host);
    const wchar_t *allowed[]={L"upload.wikimedia.org",L"commons.wikimedia.org",L"thumb.wikimedia.org",L"archive.org",L"images.unsplash.com",L"svs.gsfc.nasa.gov"};
    for(UINT n=0;n<ARRAYSIZE(allowed);++n) if(wcslen(allowed[n])==length && !_wcsnicmp(host,allowed[n],length)) return TRUE;
    return FALSE;
}
static BOOL parse_catalog(wchar_t const* path,std::vector<OnlineItem>& items) {
    items.clear();std::set<std::wstring> ids,filenames;Json json;if(!json_file(&json,path,2*1024*1024)) return FALSE;
    int entries=json_property(&json,0,"entries");int counts[3]={};BOOL ok=entries>=0 && json.tokens[entries].type==JSON_ARRAY && json_integer(&json,json_property(&json,0,"schema_version"),0)==4;
    if(ok) for(int object=entries+1;object<json.tokens[entries].next;object=json.tokens[object].next) {
        if(items.size()>=120 || json.tokens[object].type!=JSON_OBJECT) { ok=FALSE;break; }
        OnlineItem item={};wchar_t kind[20];get_string(&json,object,"kind",kind,20);item.kind=!wcscmp(kind,L"image")?MEDIA_IMAGE:!wcscmp(kind,L"gif")?MEDIA_GIF:!wcscmp(kind,L"video")?MEDIA_VIDEO:MEDIA_UNKNOWN;
        int title=json_property(&json,object,"title");get_string(&json,title,g_app.english?"en":"zh",item.title,180);
        get_string(&json,object,"id",item.id,100);get_string(&json,object,"filename",item.filename,180);get_string(&json,object,"download_url",item.url,4096);get_string(&json,object,"thumbnail_url",item.thumbnail,4096);get_string(&json,object,"license",item.license,100);get_string(&json,object,"author",item.author,300);get_string(&json,object,"source_url",item.source,4096);get_string(&json,object,"sha1",item.sha1,41);item.bytes=(uint64_t)json_integer(&json,json_property(&json,object,"bytes"),0);
        BOOL hash=wcslen(item.sha1)==40;for(UINT k=0;k<wcslen(item.sha1);++k) if(!iswxdigit(item.sha1[k])) hash=FALSE;
        if(item.kind==MEDIA_UNKNOWN || ++counts[item.kind]>80 || !*item.title || !safe_filename(item.id) || !safe_filename(item.filename) || !catalog_host(item.url) || !catalog_host(item.thumbnail) || !safe_https_url(item.source) || !*item.license || !hash || item.bytes<1 || item.bytes>20000000 || json_integer(&json,json_property(&json,object,"width"),0)<1920 || json_integer(&json,json_property(&json,object,"height"),0)<1080) { ok=FALSE;break; }
        auto width=json_integer(&json,json_property(&json,object,"width"),0),height=json_integer(&json,json_property(&json,object,"height"),0);if(width>32768||height>32768||(item.kind==MEDIA_GIF&&width*9!=height*16)){ok=FALSE;break;}
        wchar_t directory[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"online");path_join(item.local,LB_PATH,directory,item.filename);if(!ids.insert(item.id).second||!filenames.insert(item.filename).second){ok=FALSE;break;}items.push_back(item);
    }
    json_free(&json);if(!ok||items.empty()){items.clear();return FALSE;}return TRUE;
}

static std::wstring catalog_loaded_path;
static bool reserve_items(size_t count){
    if(count<=online.capacity)return true;if(count>120)return false;
    auto memory=static_cast<OnlineItem*>(realloc(online.items,count*sizeof(OnlineItem)));if(!memory)return false;
    online.items=memory;online.capacity=UINT(count);return true;
}
static int catalog_revision(std::filesystem::path const& path){
    Json json;if(!json_file(&json,path.c_str(),2*1024*1024))return 0;
    int revision=int(json_integer(&json,json_property(&json,0,"catalog_revision"),0));json_free(&json);return revision;
}
static bool set_catalog(std::vector<OnlineItem> const& items,std::filesystem::path const& path){
    if(!reserve_items(items.size()))return false;
    std::copy(items.begin(),items.end(),online.items);online.count=UINT(items.size());catalog_loaded_path=path.wstring();return true;
}
static BOOL load_catalog(void){
    if(online.thread)return online.items&&online.count;std::vector<OnlineItem> items;auto cached=std::filesystem::path(g_app.data)/L"online"/L"catalog-v4.json";
    auto seed=std::filesystem::path(g_app.directory)/L"assets"/L"online-wallpapers-v2.json";
    if(catalog_revision(cached)>=catalog_revision(seed)&&parse_catalog(cached.c_str(),items))return set_catalog(items,cached);
    return parse_catalog(seed.c_str(),items)&&set_catalog(items,seed);
}
static wchar_t const* active_catalog_path(){if(catalog_loaded_path.empty())load_catalog();return catalog_loaded_path.c_str();}
static int install_catalog(wchar_t const* file){
    if(!file||online.thread)return 0;
    try{
        std::vector<OnlineItem> items;if(!parse_catalog(file,items))return 0;
        auto seed=std::filesystem::path(g_app.directory)/L"assets"/L"online-wallpapers-v2.json";if(catalog_revision(file)<std::max(catalog_revision(seed),catalog_revision(catalog_loaded_path)))return 0;
        // Allocate before committing; a memory failure must not replace a usable catalog.
        if(!reserve_items(items.size()))return 0;
        auto directory=std::filesystem::path(g_app.data)/L"online";if(!ensure_directory(directory.c_str()))return 0;auto target=directory/L"catalog-v4.json",part=directory/(L"catalog-"+std::to_wstring(GetTickCount64())+L".partial");lb::scope_exit cleanup([&]{DeleteFileW(part.c_str());});
        if(!CopyFileW(file,part.c_str(),TRUE)||!MoveFileExW(part.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return 0;
        return set_catalog(items,target);
    }catch(...){return 0;}
}
UiCatalogApi const* online_catalog_api(){static UiCatalogApi api{LB_ONLINE_CATALOG_ABI,sizeof(UiCatalogApi),active_catalog_path,install_catalog};return &api;}
bool online_catalog_refresh_tests(wchar_t const* directory){
    try{
        if(!catalog_host(L"https://svs.gsfc.nasa.gov/vis/aurora.gif")||!catalog_host(L"https://SVS.GSFC.NASA.GOV/vis/moon.mp4")||catalog_host(L"https://svs.gsfc.nasa.gov.evil.example/a.gif")||catalog_host(L"https://svs.gsfc.nasa.gov@evil.example/a.gif")||catalog_host(L"http://svs.gsfc.nasa.gov/a.gif"))return false;
        std::filesystem::path root(directory);if(!ensure_directory(root.c_str()))return false;auto seed=std::filesystem::path(g_app.directory)/L"assets"/L"online-wallpapers-v2.json";auto valid=root/L"valid.json";if(!CopyFileW(seed.c_str(),valid.c_str(),TRUE))return false;
        std::vector<OnlineItem> expected;if(!parse_catalog(seed.c_str(),expected)||!install_catalog(valid.c_str())||online.count!=expected.size())return false;auto before=catalog_loaded_path;auto count=online.count;wchar_t digest[65]{},after[65]{};if(!hash_file(before.c_str(),L"SHA256",digest,65))return false;
        auto bad=root/L"invalid.json";if(!write_utf8_atomic(bad.c_str(),L"{\"schema_version\":4,\"entries\":[]}")||install_catalog(bad.c_str())||online.count!=count||before!=catalog_loaded_path)return false;
        if(!hash_file(before.c_str(),L"SHA256",after,65)||wcscmp(digest,after))return false;
        auto saved=online.thread;online.thread=reinterpret_cast<HANDLE>(1);bool refused=install_catalog(valid.c_str())==0;online.thread=saved;if(!refused)return false;
        auto previews=std::filesystem::path(g_app.data)/L"previews";if(!ensure_directory(previews.c_str()))return false;auto first=previews/L"budget-first.mp4",second=previews/L"budget-second.mp4",pinned=previews/L"budget-current.mp4",unmanaged=previews/L"keep-unmanaged.txt";
        for(auto const& path:{first,second,pinned}){if(!write_utf8_atomic(path.c_str(),L"0123456789012345678901234567890123456789")||!write_utf8_atomic((path.wstring()+L".verified").c_str(),L"fixture receipt"))return false;}
        if(!write_utf8_atomic(unmanaged.c_str(),L"Unmanaged files must not be removed."))return false;std::wstring previous=online.job_path;lb::scope_exit restore_job([&]{StringCchCopyW(online.job_path,LB_PATH,previous.c_str());});StringCchCopyW(online.job_path,LB_PATH,pinned.c_str());prune_previews(40);
        if(std::filesystem::exists(first)||std::filesystem::exists(second)||!std::filesystem::exists(pinned)||!std::filesystem::exists(unmanaged))return false;
        return write_utf8_atomic((root/L"result.json").c_str(),L"{\"valid_catalog_committed\":true,\"empty_rejected\":true,\"prior_catalog_preserved\":true,\"active_download_blocks_replacement\":true,\"preview_cache_budget_and_ownership\":true,\"network_requests\":0}");
    }catch(...){return false;}
}
UINT online_catalog_test(void) { BOOL valid=load_catalog();UINT count=valid?online.count:0;if(!online.window) { free(online.items);online.items=NULL;online.count=online.capacity=0; }return count; }
static void refresh(void) {
    wchar_t query[260];GetWindowTextW(online.search,query,260);int filter=(int)SendMessageW(online.filter,CB_GETCURSEL,0,0);
    SendMessageW(online.list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(online.list);
    for(UINT n=0;n<online.count;++n) { OnlineItem *item=&online.items[n];if((int)item->kind!=filter || (*query && !StrStrIW(item->title,query) && !StrStrIW(item->author,query))) continue;
        LVITEMW row={};row.mask=LVIF_TEXT|LVIF_PARAM;row.iItem=ListView_GetItemCount(online.list);row.pszText=item->title;row.lParam=n;int index=ListView_InsertItem(online.list,&row);wchar_t size[40];StringCchPrintfW(size,40,L"%.1f MB",(double)item->bytes/1000000.);ListView_SetItemText(online.list,index,1,size);ListView_SetItemText(online.list,index,2,item->license);ListView_SetItemText(online.list,index,3,(wchar_t*)(downloaded(*item)?tr(L"已下载",L"Downloaded"):L""));
    }SendMessageW(online.list,WM_SETREDRAW,TRUE,0);InvalidateRect(online.list,NULL,TRUE);
}
static int selection(void) { int index=ListView_GetNextItem(online.list,-1,LVNI_SELECTED);if(index<0) return -1;LVITEMW row={};row.mask=LVIF_PARAM;row.iItem=index;if(!ListView_GetItem(online.list,&row) || (UINT)row.lParam>=online.count) return -1;return (int)row.lParam; }
static void progress(uint64_t received,uint64_t total,void*) {
    online.received=received;online.total=total;online.activity=GetTickCount64();
    PostMessageW(online.window?online.window:g_app.window,WM_ONLINE_PROGRESS,total?WPARAM(std::min<uint64_t>(99,received*100/total)):0,0);
}
static void stage_changed(DownloadStage stage,void*){online.stage=int(stage);online.activity=GetTickCount64();}
static void reset_progress(uint64_t total){online.received=0;online.total=total;online.started=online.activity=GetTickCount64();online.stage=int(DownloadStage::connecting);online.percent=0;}
static DWORD WINAPI download_thread(void *unused) {
    (void)unused;OnlineItem *item=&online.items[online.job];
    auto cached=std::filesystem::path(g_app.data)/L"previews"/item->filename;bool reused=false;
    if(!online.preview_job&&downloaded(*item,cached.c_str())) {
        auto temporary=std::wstring(online.job_path)+L".partial";wchar_t actual[41]{};
        if(CopyFileW(cached.c_str(),temporary.c_str(),FALSE)&&hash_file(temporary.c_str(),L"SHA1",actual,41)&&!_wcsicmp(actual,item->sha1)&&!InterlockedCompareExchange(&online.cancel,0,0))reused=MoveFileExW(temporary.c_str(),online.job_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!reused)DeleteFileW(temporary.c_str());
    }
    online.result=reused?S_OK:download_https(item->url,online.job_path,20000000,item->bytes,L"SHA1",item->sha1,&online.cancel,progress,NULL,stage_changed);
    if(SUCCEEDED(online.result)) { auto metadata=receipt(*item,online.job_path);if(!metadata.empty()) write_utf8_atomic((std::wstring(online.job_path)+L".verified").c_str(),metadata.c_str()); }
    PostMessageW(online.window?online.window:g_app.window,WM_ONLINE_DONE,0,0);return 0;
}
static void start_download(BOOL preview) {
    int selected=selection();if(selected<0 || online.thread) return;OnlineItem *item=&online.items[selected];online.job=(UINT)selected;online.preview_job=preview;
    if(preview) { if(downloaded(*item)) { online.result=S_OK;report_download_result();player_request(item->local,FALSE);return; }wchar_t directory[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"previews");ensure_directory(directory);path_join(online.job_path,LB_PATH,directory,item->filename); }
    else StringCchCopyW(online.job_path,LB_PATH,item->local);
    reset_progress(item->bytes);InterlockedExchange(&online.cancel,0);EnableWindow(online.download,FALSE);EnableWindow(online.preview,FALSE);SendMessageW(online.progress,PBM_SETPOS,0,0);online.thread=CreateThread(NULL,0,download_thread,NULL,0,NULL);
    if(!online.thread) { EnableWindow(online.download,TRUE);EnableWindow(online.preview,TRUE);error_box(online.window,tr(L"无法创建下载任务",L"Could not start download"),E_FAIL); }
}
static LRESULT CALLBACK online_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        online.window=window;online.brush=CreateSolidBrush(g_app.dark?RGB(26,33,45):RGB(246,247,250));
        online.filter=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,22,22,140,180,window,(HMENU)1,g_app.instance,NULL);const wchar_t *types[]={tr(L"静态",L"Images"),L"GIF",tr(L"视频",L"Videos")};for(int n=0;n<3;++n) SendMessageW(online.filter,CB_ADDSTRING,0,(LPARAM)types[n]);SendMessageW(online.filter,CB_SETCURSEL,0,0);
        online.search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,180,22,500,32,window,(HMENU)2,g_app.instance,NULL);SendMessageW(online.search,EM_SETCUEBANNER,TRUE,(LPARAM)tr(L"搜索标题或作者…",L"Search titles or authors…"));
        online.list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL,22,76,850,350,window,(HMENU)3,g_app.instance,NULL);ListView_SetExtendedListViewStyle(online.list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);const wchar_t *names[]={tr(L"壁纸",L"Wallpaper"),tr(L"原文件",L"Original size"),tr(L"授权",L"License"),tr(L"状态",L"Status")};int widths[]={390,120,160,140};for(int n=0;n<4;++n) { LVCOLUMNW col={};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(wchar_t*)names[n];col.cx=widths[n];ListView_InsertColumn(online.list,n,&col); }
        online.details=CreateWindowExW(0,L"STATIC",tr(L"公开授权精选 · 原生至少 1080p · 原文件不超过 20 MB",L"Licensed collection · Native 1080p+ · Original files under 20 MB"),WS_CHILD|WS_VISIBLE,22,442,850,60,window,NULL,g_app.instance,NULL);
        online.progress=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE,22,514,850,13,window,NULL,g_app.instance,NULL);SendMessageW(online.progress,PBM_SETRANGE32,0,100);
        online.preview=CreateWindowExW(0,L"BUTTON",tr(L"预览原文件",L"Preview original"),WS_CHILD|WS_VISIBLE,22,546,150,36,window,(HMENU)10,g_app.instance,NULL);
        online.download=CreateWindowExW(0,L"BUTTON",tr(L"下载原文件",L"Download original"),WS_CHILD|WS_VISIBLE,188,546,155,36,window,(HMENU)11,g_app.instance,NULL);
        CreateWindowExW(0,L"BUTTON",tr(L"导入到壁纸库",L"Add to library"),WS_CHILD|WS_VISIBLE,360,546,155,36,window,(HMENU)12,g_app.instance,NULL);CreateWindowExW(0,L"BUTTON",tr(L"来源与授权 ↗",L"Source / license ↗"),WS_CHILD|WS_VISIBLE,532,546,165,36,window,(HMENU)13,g_app.instance,NULL);CreateWindowExW(0,L"BUTTON",tr(L"取消下载",L"Cancel download"),WS_CHILD|WS_VISIBLE,714,546,155,36,window,(HMENU)14,g_app.instance,NULL);
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) SendMessageW(child,WM_SETFONT,(WPARAM)g_app.font,TRUE);
        ListView_SetBkColor(online.list,g_app.dark?RGB(26,33,45):RGB(255,255,255));ListView_SetTextBkColor(online.list,CLR_NONE);ListView_SetTextColor(online.list,g_app.dark?RGB(235,240,249):RGB(29,38,49));if(!load_catalog()) error_box(window,tr(L"在线目录校验失败",L"Online catalog failed validation"),E_INVALIDARG);refresh();return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==1 && HIWORD(wp)==CBN_SELCHANGE) refresh();else if(LOWORD(wp)==2 && HIWORD(wp)==EN_CHANGE) SetTimer(window,1,180,NULL);
        else if(LOWORD(wp)==10) start_download(TRUE);else if(LOWORD(wp)==11) start_download(FALSE);else if(LOWORD(wp)==14) InterlockedExchange(&online.cancel,1);
        else { int n=selection();if(n<0) return 0;OnlineItem *item=&online.items[n];if(LOWORD(wp)==12) { if(downloaded(*item)) library_import_checked(item->local,item->sha1);else error_box(window,tr(L"请先完整下载并校验原文件",L"Download and verify the original first"),HRESULT_FROM_WIN32(ERROR_CRC)); }else if(LOWORD(wp)==13) ShellExecuteW(window,L"open",item->source,NULL,NULL,SW_SHOWNORMAL); }return 0;
    case WM_NOTIFY:if(((NMHDR*)lp)->hwndFrom==online.list && ((NMHDR*)lp)->code==LVN_ITEMCHANGED) { int n=selection();if(n>=0) { wchar_t text[800];OnlineItem *item=&online.items[n];StringCchPrintfW(text,800,L"%s  ·  %s\n%s",item->title,item->license,item->author);SetWindowTextW(online.details,text); } }return 0;
    case WM_TIMER:KillTimer(window,1);refresh();return 0;
    case WM_ONLINE_PROGRESS:online.percent=int(wp);SendMessageW(online.progress,PBM_SETPOS,wp,0);return 0;
    case WM_ONLINE_DONE:
        if(online.thread) { CloseHandle(online.thread);online.thread=NULL; }EnableWindow(online.download,TRUE);EnableWindow(online.preview,TRUE);
        ++online.serial;online.percent=SUCCEEDED(online.result)?100:0;report_download_result();
        if(SUCCEEDED(online.result)) { if(online.preview_job&&!g_app.close_requested) player_request(online.job_path,FALSE);else SetWindowTextW(online.details,tr(L"原文件下载和 SHA-1 校验完成，可以导入壁纸库。",L"Original downloaded and SHA-1 verified. You can add it to your library.")); }
        else if(!g_app.close_requested && online.result!=HRESULT_FROM_WIN32(ERROR_CANCELLED)) error_box(window,tr(L"下载失败或内容校验不一致",L"Download failed or checksum did not match"),online.result);refresh();return 0;
    case WM_CTLCOLORSTATIC:SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,g_app.dark?RGB(235,240,249):RGB(29,38,49));return (LRESULT)online.brush;
    case WM_ERASEBKGND: { RECT r;GetClientRect(window,&r);FillRect((HDC)wp,&r,online.brush);return 1; }
    case WM_CLOSE:ShowWindow(window,SW_HIDE);return 0;
    case WM_DESTROY:DeleteObject(online.brush);free(online.items);online.items=NULL;online.count=0;online.window=NULL;return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void online_show(void) {
    if(online.window) { ShowWindow(online.window,SW_RESTORE);SetForegroundWindow(online.window);return; }
    WNDCLASSW cls={};cls.lpfnWndProc=online_proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Online";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);RegisterClassW(&cls);HWND window=CreateWindowExW(0,cls.lpszClassName,tr(L"在线壁纸",L"Online wallpapers"),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,140,90,916,650,g_app.window,NULL,g_app.instance,NULL);if(window) { theme_window(window);ShowWindow(window,SW_SHOW); }
}
BOOL online_shutdown(void) {
    if(online.thread) {
        InterlockedExchange(&online.cancel,1);
        if(WaitForSingleObject(online.thread,0)!=WAIT_OBJECT_0) return FALSE;
        // A hidden/destroyed page can lose its completion notification.
        CloseHandle(online.thread);online.thread=nullptr;
    }
    if(online.window) DestroyWindow(online.window);return TRUE;
}
void online_snapshot(UiState* out) { out->download_busy=online.thread!=nullptr;out->download_percent=online.percent;out->download_index=int(online.job);out->download_preview=online.preview_job;out->download_error=!online.thread&&FAILED(online.result);out->download_serial=online.serial;StringCchCopyW(out->download_path,LB_UI_PATH,online.job_path);out->download_received=online.received.load();out->download_total=online.total.load();out->download_stage=online.thread&&InterlockedCompareExchange(&online.cancel,0,0)?4:online.stage.load();out->download_elapsed=online.thread?int((GetTickCount64()-online.started)/1000):0;out->download_idle=online.thread?int((GetTickCount64()-online.activity.load())/1000):0; }
bool online_message(UINT message,WPARAM wp,LPARAM) {
    if(online.window) return false;if(message==WM_ONLINE_PROGRESS) { online.percent=int(wp);return true; }if(message!=WM_ONLINE_DONE) return false;
    if(online.thread) { CloseHandle(online.thread);online.thread=nullptr; }++online.serial;online.percent=SUCCEEDED(online.result)?100:0;
    report_download_result();return true;
}
void online_start(int index,int preview) {
    if(g_app.close_requested) return;
    if(index==-1) { InterlockedExchange(&online.cancel,1);return; }if(online.thread) return;if(!online.items&&!load_catalog()) return;if(index<0||index>=int(online.count)) return;auto const& item=online.items[index];if(preview==2||preview==3) { if(downloaded(item)){if(preview==3)library_import_apply_checked(item.local,item.sha1);else library_import_checked(item.local,item.sha1);}else set_status(L"请先完整下载壁纸",L"Download the wallpaper first.");return; }online.job=UINT(index);online.preview_job=preview!=0;online.percent=0;online.result=S_OK;
    if(downloaded(item)) { StringCchCopyW(online.job_path,LB_PATH,item.local);online.percent=100;++online.serial;report_download_result();return; }
    if(preview) { auto directory=std::filesystem::path(g_app.data)/L"previews";ensure_directory(directory.c_str());StringCchCopyW(online.job_path,LB_PATH,(directory/item.filename).c_str()); }else {ensure_directory((std::filesystem::path(g_app.data)/L"online").c_str());StringCchCopyW(online.job_path,LB_PATH,item.local);}
    if(preview&&downloaded(item,online.job_path)){online.percent=100;++online.serial;report_download_result();return;}
    reset_progress(item.bytes);set_status(L"正在连接壁纸来源…",L"Connecting to the wallpaper source…");InterlockedExchange(&online.cancel,0);online.thread=CreateThread(nullptr,0,download_thread,nullptr,0,nullptr);if(!online.thread) { online.result=HRESULT_FROM_WIN32(GetLastError());++online.serial;report_download_result(); }
}
