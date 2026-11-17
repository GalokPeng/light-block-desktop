#include "lb.h"
#include "calculator.hpp"
#include "organizer.hpp"
#include "directory_watch.hpp"
#include "frontend.hpp"
#include "launcher_ui_api.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <set>

namespace {
struct Entry { std::wstring path,name,kind;int command{};bool folder{};uint64_t id{};std::wstring key; };
struct Index {
    std::vector<std::wstring> desktop,menus;std::wstring data;HWND owner{};bool english{};
    std::vector<Entry> entries;std::atomic_bool stop{},done{};
    lb::unique_handle finished{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
};
std::shared_ptr<Index> index;
std::thread index_thread;
std::vector<Entry> entries,displayed;
HWND window{},search{},list{},status{};bool indexed{},dirty{};
bool client_open{},index_english{};uint64_t next_id{1};uint32_t revision{1};ULONGLONG indexed_at{};
std::optional<std::wstring> result;
lb::DirectoryWatch watcher;
constexpr uint64_t command_id=uint64_t(1)<<63;
void ensure_window(bool legacy=false);
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
std::wstring lower(std::wstring value) { if(!value.empty()) CharLowerBuffW(value.data(),DWORD(value.size()));return value; }
template<size_t N> void copy(wchar_t(&destination)[N],std::wstring const& source) { StringCchCopyNW(destination,N,source.c_str(),N-1); }
std::wstring trim(std::wstring value) { auto begin=value.find_first_not_of(L" \t\r\n");if(begin==value.npos)return {};auto end=value.find_last_not_of(L" \t\r\n");return value.substr(begin,end-begin+1); }
std::vector<std::wstring> tokens(std::wstring const& query) { std::vector<std::wstring> out;size_t at=0;while(at<query.size()) { while(at<query.size()&&iswspace(query[at]))++at;auto first=at;while(at<query.size()&&!iswspace(query[at]))++at;if(first<at)out.push_back(query.substr(first,at-first)); }return out; }
std::vector<Entry> tools(bool english) {
    struct Tool { wchar_t const* zh;wchar_t const* en;int command; };
    static constexpr Tool commands[]={{L"截图工作室",L"Screenshot studio",701},{L"录制屏幕 / 停止录屏",L"Record screen / Stop recording",712},{L"视频裁剪与 GIF 导出",L"Video trim and GIF export",710},{L"剪贴板历史",L"Clipboard history",707},{L"便签与任务",L"Notes and tasks",705},{L"文件工作台",L"File workbench",708},{L"专注工作区",L"Focus workspace",709}};
    std::vector<Entry> out;for(auto const& tool:commands) { Entry entry;entry.name=english?tool.en:tool.zh;entry.kind=english?L"Light Block tool":L"轻块桌面工具";entry.command=tool.command;entry.id=command_id|uint64_t(tool.command);entry.key=lower(entry.name);out.push_back(std::move(entry)); }return out;
}
std::vector<Entry const*> ranked(std::wstring const& query,std::vector<Entry> const& data,std::vector<Entry> const& commands) {
    auto words=tokens(query);std::vector<Entry const*> found;found.reserve(data.size()+commands.size());
    auto collect=[&](auto const& source) { for(auto const& entry:source)if(std::all_of(words.begin(),words.end(),[&](auto const& token){return entry.key.find(token)!=entry.key.npos;}))found.push_back(&entry); };
    collect(commands);collect(data);auto count=std::min<size_t>(16,found.size());
    std::partial_sort(found.begin(),found.begin()+count,found.end(),[&](auto a,auto b){bool ap=a->key.starts_with(query),bp=b->key.starts_with(query);if(ap!=bp)return ap>bp;if(a->key!=b->key)return a->key<b->key;return a->id<b->id;});found.resize(count);return found;
}
using IconPixels=std::array<uint32_t,LB_LAUNCHER_ICON_SIZE*LB_LAUNCHER_ICON_SIZE>;
struct IconCache {
    std::mutex mutex;std::condition_variable changed;std::deque<std::wstring> queue;
    std::set<std::wstring> pending;std::map<std::wstring,IconPixels> ready;std::set<std::wstring> missing;
    std::atomic_bool stop{};std::atomic<uint32_t> revision{};lb::unique_handle finished{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
};
std::shared_ptr<IconCache> icons;std::thread icon_thread;
bool read_icon(std::wstring const& path,IconPixels &pixels) {
    SHFILEINFOW info{};DWORD_PTR ok{};
    if(path.starts_with(L"shell:")) { PIDLIST_ABSOLUTE pidl{};if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&pidl,0,nullptr))) { ok=SHGetFileInfoW(reinterpret_cast<wchar_t const*>(pidl),0,&info,sizeof info,SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON);CoTaskMemFree(pidl); } }
    else ok=SHGetFileInfoW(path.c_str(),0,&info,sizeof info,SHGFI_ICON|SHGFI_LARGEICON);
    if(!ok||!info.hIcon)return false;lb::scope_exit release([&]{DestroyIcon(info.hIcon);});
    BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=LB_LAUNCHER_ICON_SIZE;bitmap.bmiHeader.biHeight=-LB_LAUNCHER_ICON_SIZE;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;lb::scope_exit release_dc([&]{DeleteDC(dc);});void *data{};HBITMAP dib=CreateDIBSection(dc,&bitmap,DIB_RGB_COLORS,&data,nullptr,0);if(!dib)return false;auto old=SelectObject(dc,dib);lb::scope_exit release_dib([&]{SelectObject(dc,old);DeleteObject(dib);});
    std::fill_n(static_cast<uint32_t*>(data),pixels.size(),0u);if(!DrawIconEx(dc,0,0,info.hIcon,32,32,0,nullptr,DI_NORMAL))return false;std::copy_n(static_cast<uint32_t*>(data),pixels.size(),pixels.begin());
    std::fill_n(static_cast<uint32_t*>(data),pixels.size(),0x00ffffffu);DrawIconEx(dc,0,0,info.hIcon,32,32,0,nullptr,DI_NORMAL);
    auto white=static_cast<uint32_t const*>(data);for(size_t n=0;n<pixels.size();++n) { int difference=std::max({int(white[n]&255)-int(pixels[n]&255),int((white[n]>>8)&255)-int((pixels[n]>>8)&255),int((white[n]>>16)&255)-int((pixels[n]>>16)&255)});auto alpha=uint32_t(std::clamp(255-difference,0,255));pixels[n]=(pixels[n]&0x00ffffffu)|(alpha<<24); }return true;
}
void begin_icons() {
    if(icons)return;icons=std::make_shared<IconCache>();auto state=icons;icon_thread=std::thread([state]{lb::com_apartment apartment(COINIT_MULTITHREADED);lb::scope_exit finished([&]{SetEvent(state->finished.get());});while(!state->stop) { std::wstring path;{std::unique_lock lock(state->mutex);state->changed.wait(lock,[&]{return state->stop||!state->queue.empty();});if(state->stop)break;path=std::move(state->queue.front());state->queue.pop_front();}IconPixels pixels{};bool ok=read_icon(path,pixels);{std::lock_guard lock(state->mutex);state->pending.erase(path);if(state->ready.size()>=256)state->ready.erase(state->ready.begin());if(state->missing.size()>=256)state->missing.clear();if(ok)state->ready.emplace(path,std::move(pixels));else state->missing.insert(path);}++state->revision;}});
}
void end_icons() {
    if(!icons)return;icons->stop=true;icons->changed.notify_all();if(icon_thread.joinable()){if(WaitForSingleObject(icons->finished.get(),200)==WAIT_OBJECT_0)icon_thread.join();else icon_thread.detach();}icons.reset();
}
void scan(std::filesystem::path const& root,bool recursive,std::vector<Entry> &items,std::set<std::wstring> &known,std::atomic_bool &stop,bool english,int depth=0) {
    if(stop||depth>16||items.size()>=10000) return;WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((root/L"*").c_str(),&data);if(find==INVALID_HANDLE_VALUE) return;
    lb::scope_exit close([&]{FindClose(find);});
    do {
        if(stop||items.size()>=10000) break;if(data.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)) continue;if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L"..")) continue;
        auto path=root/data.cFileName;bool folder=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if(recursive&&folder) { if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) scan(path,true,items,known,stop,english,depth+1);continue; }
        auto extension=lower(path.extension().wstring());if(recursive&&extension!=L".lnk"&&extension!=L".url"&&extension!=L".appref-ms") continue;
        if(!known.insert(lower(path.wstring())).second) continue;
        auto name=path.filename().wstring();if(!folder&&(extension==L".lnk"||extension==L".url"||extension==L".appref-ms")) name=path.stem().wstring();
        items.push_back({path.wstring(),std::move(name),recursive?(english?L"Application":L"应用程序"):(english?L"Desktop":L"桌面"),0,folder});
    }while(FindNextFileW(find,&data));
}
std::vector<std::wstring> folders(std::initializer_list<KNOWNFOLDERID> ids) {
    std::vector<std::wstring> roots;for(auto const& id:ids) { PWSTR path=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&path))) { roots.emplace_back(path);CoTaskMemFree(path); } }return roots;
}
void end_index() {
    if(!index) return;index->stop=true;if(index_thread.joinable()) { if(WaitForSingleObject(index->finished.get(),1000)==WAIT_OBJECT_0) index_thread.join();else index_thread.detach(); }index.reset();
}
void begin_index() {
    if(index&&!index->done) return;if(index) end_index();auto state=std::make_shared<Index>();state->desktop=folders({FOLDERID_Desktop,FOLDERID_PublicDesktop});state->menus=folders({FOLDERID_Programs,FOLDERID_CommonPrograms});state->data=g_app.data;state->owner=window;state->english=g_app.english;index=state;dirty=false;
    index_thread=std::thread([state] {
        lb::com_apartment apartment(COINIT_MULTITHREADED);lb::scope_exit done([&]{state->done=true;SetEvent(state->finished.get());PostMessageW(state->owner,WM_APP+134,0,0);});
        try {
            std::set<std::wstring> known;
            state->entries.push_back({L"shell:MyComputerFolder",state->english?L"This PC":L"此电脑",state->english?L"System":L"系统"});state->entries.push_back({L"shell:RecycleBinFolder",state->english?L"Recycle Bin":L"回收站",state->english?L"System":L"系统"});
            for(auto const& root:state->desktop) scan(root,false,state->entries,known,state->stop,state->english);
            for(auto const& root:state->menus) scan(root,true,state->entries,known,state->stop,state->english);
            lb::organizer::Model model(state->data);if(model.load()) for(auto const& item:model.entries) if(!item.system&&known.insert(lower(item.path)).second) { auto group=std::find_if(model.groups.begin(),model.groups.end(),[&](auto const& g){return g.id==item.group;});state->entries.push_back({item.path,item.name,group==model.groups.end()?(state->english?L"Organizer":L"收纳"):group->name});if(state->entries.size()>=10000) break; }
            for(auto &entry:state->entries)entry.key=lower(entry.name);
            std::stable_sort(state->entries.begin(),state->entries.end(),[](auto const& a,auto const& b){return a.key<b.key;});
        }catch(...) {}
    });
    auto roots=state->desktop;roots.insert(roots.end(),state->menus.begin(),state->menus.end());watcher.start(roots,window,WM_APP+135,0,true);
}
void refresh() {
    if(!window||!search||!list) return;wchar_t text[512];GetWindowTextW(search,text,512);result=lb::calculate(text);displayed.clear();SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    if(result) displayed.push_back({*result,std::wstring(text)+L" = "+*result,tr(L"回车复制结果",L"Enter to copy result"),-1});
    auto commands=tools(g_app.english);for(auto const* entry:ranked(lower(trim(text)),entries,commands))displayed.push_back(*entry);
    for(size_t n=0;n<displayed.size();++n) { auto &entry=displayed[n];LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(n);item.pszText=entry.name.data();int at=ListView_InsertItem(list,&item);ListView_SetItemText(list,at,1,entry.kind.data()); }
    if(!displayed.empty()) ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);wchar_t label[200];StringCchPrintfW(label,200,tr(L"%u 个已索引项目 · ↑↓ 选择 · Enter 打开 · Esc 关闭%s",L"%u indexed items · Arrows select · Enter opens · Escape closes%s"),UINT(entries.size()),indexed?L"":tr(L" · 正在后台索引…",L" · Indexing…"));SetWindowTextW(status,label);
}
void activate() {
    int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected<0||size_t(selected)>=displayed.size()) return;auto entry=displayed[size_t(selected)];
    if(entry.command==-1) {
        auto bytes=(entry.path.size()+1)*sizeof(wchar_t);HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!data) return;void *memory=GlobalLock(data);if(!memory) { GlobalFree(data);return; }memcpy(memory,entry.path.c_str(),bytes);GlobalUnlock(data);
        if(!OpenClipboard(window)) { GlobalFree(data);return; }EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,data)) GlobalFree(data);CloseClipboard();ShowWindow(window,SW_HIDE);
    }else if(entry.command) { ShowWindow(window,SW_HIDE);PostMessageW(g_app.window,WM_COMMAND,entry.command,0); }
    else { auto opened=ShellExecuteW(window,L"open",entry.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);if(INT_PTR(opened)>32) ShowWindow(window,SW_HIDE);else SetWindowTextW(status,tr(L"项目无法打开，请检查路径。",L"This item could not be opened. Check its path.")); }
}
LRESULT CALLBACK key_proc(HWND control,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_KEYDOWN) {
        if(wp==VK_RETURN) { activate();return 0; }if(wp==VK_ESCAPE) { ShowWindow(window,SW_HIDE);return 0; }
        if(control==search&&(wp==VK_UP||wp==VK_DOWN)&&!displayed.empty()) { int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);int next=(std::max(0,selected)+(wp==VK_UP?-1:1)+int(displayed.size()))%int(displayed.size());ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,next,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,next,FALSE);return 0; }
    }if(message==WM_NCDESTROY) RemoveWindowSubclass(control,key_proc,id);return DefSubclassProc(control,message,wp,lp);
}
LRESULT CALLBACK proc(HWND owner,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        window=owner;if(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams){begin_index();return 0;}search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,1,1,owner,reinterpret_cast<HMENU>(1),g_app.instance,nullptr);SendMessageW(search,EM_SETCUEBANNER,TRUE,LPARAM(tr(L"搜索应用、收纳内容，或输入 24 × 60",L"Find apps and desktop items, or enter 24 * 60")));SendMessageW(search,WM_SETFONT,WPARAM(g_app.heading),TRUE);SetWindowSubclass(search,key_proc,1,0);
        list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER,0,0,1,1,owner,reinterpret_cast<HMENU>(2),g_app.instance,nullptr);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SendMessageW(list,WM_SETFONT,WPARAM(g_app.font),TRUE);SetWindowSubclass(list,key_proc,1,0);
        for(int n=0;n<2;++n) { LVCOLUMNW column{};column.mask=LVCF_WIDTH;column.cx=px(n?140:450);ListView_InsertColumn(list,n,&column); }
        ListView_SetBkColor(list,g_app.dark?RGB(26,33,45):RGB(255,255,255));ListView_SetTextBkColor(list,CLR_NONE);ListView_SetTextColor(list,g_app.dark?RGB(235,240,249):RGB(29,38,49));
        status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,owner,nullptr,g_app.instance,nullptr);SendMessageW(status,WM_SETFONT,WPARAM(g_app.small),TRUE);refresh();begin_index();return 0;
    }
    case WM_SIZE:MoveWindow(search,px(18),px(18),LOWORD(lp)-px(36),px(46),TRUE);MoveWindow(list,px(18),px(80),LOWORD(lp)-px(36),std::max(px(60),int(HIWORD(lp))-px(132)),TRUE);MoveWindow(status,px(18),HIWORD(lp)-px(36),LOWORD(lp)-px(36),px(28),TRUE);ListView_SetColumnWidth(list,0,std::max(px(200),int(LOWORD(lp))-px(210)));return 0;
    case WM_COMMAND:if(LOWORD(wp)==1&&HIWORD(wp)==EN_CHANGE) SetTimer(owner,1,65,nullptr);return 0;
    case WM_TIMER:KillTimer(owner,UINT_PTR(wp));if(wp==1) refresh();else if(wp==2) begin_index();else if(wp==3&&!client_open&&!IsWindowVisible(owner)){watcher.stop();end_index();end_icons();std::vector<Entry>().swap(entries);std::vector<Entry>().swap(displayed);indexed=false;dirty=true;result.reset();++revision;}return 0;
    case WM_APP+134:if(index&&index->done) { if(index_thread.joinable()) index_thread.join();entries=std::move(index->entries);index_english=index->english;for(auto &entry:entries)entry.id=next_id++;index.reset();indexed=true;indexed_at=GetTickCount64();++revision;if(IsWindowVisible(owner))refresh();if(dirty&&(client_open||IsWindowVisible(owner)))SetTimer(owner,2,350,nullptr); }return 0;
    case WM_APP+135:dirty=true;if(client_open||IsWindowVisible(owner)) SetTimer(owner,2,350,nullptr);return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->code==NM_DBLCLK) activate();return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(HDC(wp),g_app.dark?RGB(235,240,249):RGB(29,38,49));SetBkColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: { RECT client{};GetClientRect(owner,&client);SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));FillRect(HDC(wp),&client,HBRUSH(GetStockObject(DC_BRUSH)));return 1; }
    case WM_CLOSE:ShowWindow(owner,SW_HIDE);return 0;
    case WM_DESTROY:watcher.stop();end_index();window=search=list=status=nullptr;return 0;
    }return DefWindowProcW(owner,message,wp,lp);
}
void ensure_window(bool legacy) {
    if(window&&(!legacy||search))return;if(window){DestroyWindow(window);window=nullptr;}
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.Launcher";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
    if(!legacy){CreateWindowExW(0,cls.lpszClassName,L"Launcher service",0,0,0,0,0,HWND_MESSAGE,nullptr,g_app.instance,reinterpret_cast<void*>(1));return;}
    POINT point;GetCursorPos(&point);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTOPRIMARY),&monitor);int width=px(680),height=px(500);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,tr(L"轻块桌面 · 快速启动",L"Light Block · Quick launcher"),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/3,width,height,GetShellWindow(),nullptr,g_app.instance,nullptr);
    if(owner)theme_window(owner);
}
void service_open() { client_open=true;ensure_window();if(window)KillTimer(window,3);begin_icons();if(dirty||(!indexed&&!index)||(indexed&&(g_app.english!=index_english||GetTickCount64()-indexed_at>60000)))begin_index(); }
void service_close() { client_open=false;if(window)SetTimer(window,3,30000,nullptr); }
void service_state(LauncherUiState* out) { if(out)*out={revision+(icons?icons->revision.load():0),int(entries.size()),int(bool(index)||!indexed)}; }
int service_query(wchar_t const* text,LauncherUiResult* out,int capacity) {
    if(!out||capacity<=0)return 0;auto query=trim(text?text:L"");if(query.size()>512)query.resize(512);result=lb::calculate(query);int count{};
    if(result) { out[count]={};out[count].kind=2;copy(out[count].title,L"=  "+*result);copy(out[count].subtitle,tr(L"回车复制结果",L"Enter to copy result"));copy(out[count].target,*result);++count; }
    auto commands=tools(g_app.english);for(auto const* entry:ranked(lower(query),entries,commands)) { if(count>=capacity)break;auto &item=out[count++];item={};item.id=entry->id;item.kind=entry->command?1:0;item.command=entry->command;item.folder=entry->folder;copy(item.title,entry->name);copy(item.subtitle,entry->kind);copy(item.target,entry->path); }return count;
}
int service_icon(uint64_t id,uint32_t* out,int capacity) {
    if(!out||capacity<int(IconPixels{}.size())||!icons)return -1;auto found=std::find_if(entries.begin(),entries.end(),[&](auto const& e){return e.id==id;});if(found==entries.end())return -1;
    auto state=icons;std::lock_guard lock(state->mutex);auto ready=state->ready.find(found->path);if(ready!=state->ready.end()){std::copy(ready->second.begin(),ready->second.end(),out);return 1;}if(state->missing.contains(found->path))return -1;
    if(state->queue.size()<32&&state->pending.insert(found->path).second){state->queue.push_back(found->path);state->changed.notify_one();}return 0;
}
int service_activate(uint64_t id) {
    if(!id) { if(!result)return 0;auto bytes=(result->size()+1)*sizeof(wchar_t);HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!data)return 0;auto memory=GlobalLock(data);if(!memory){GlobalFree(data);return 0;}memcpy(memory,result->c_str(),bytes);GlobalUnlock(data);if(!OpenClipboard(window)){GlobalFree(data);return 0;}EmptyClipboard();bool ok=SetClipboardData(CF_UNICODETEXT,data)!=nullptr;if(!ok)GlobalFree(data);CloseClipboard();return ok; }
    if(id&command_id){auto commands=tools(g_app.english);auto found=std::find_if(commands.begin(),commands.end(),[&](auto const& e){return e.id==id;});if(found==commands.end())return 0;return PostMessageW(g_app.window,WM_COMMAND,WPARAM(found->command),0)!=0;}
    auto found=std::find_if(entries.begin(),entries.end(),[&](auto const& e){return e.id==id;});if(found==entries.end())return 0;return INT_PTR(ShellExecuteW(window,L"open",found->path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
}
int service_tests() {
    std::vector<Entry> sample;auto add=[&](wchar_t const* name,uint64_t id){Entry value;value.name=name;value.key=lower(name);value.id=id;sample.push_back(std::move(value));};add(L"Photo Studio",1);add(L"Studio Photo Editor",2);add(L"Photo Archive",3);auto match=ranked(L"photo studio",sample,{});if(match.size()!=2||match[0]->id!=1||match[1]->id!=2)return 0;if(ranked(L"missing",sample,{}).size())return 0;for(int n=0;n<30;++n){auto name=L"App "+std::to_wstring(n);add(name.c_str(),uint64_t(n+4));}if(ranked(L"",sample,{}).size()!=16)return 0;auto english=tools(true),chinese=tools(false);for(int command:{701,712,710,707,705,708,709})if(std::none_of(english.begin(),english.end(),[&](auto const& e){return e.command==command&&e.id==(command_id|uint64_t(command));}))return 0;if(ranked(L"gif",{},english).size()!=1||ranked(L"录屏",{},chinese).size()!=1)return 0;auto calculated=lb::calculate(L"24 × 60");if(!calculated||*calculated!=L"1440"||lb::calculate(L"1/0"))return 0;
    if(!window){service_open();bool lightweight=window&&!search&&!list&&!status&&!IsWindowVisible(window)&&icons;service_close();SendMessageW(window,WM_TIMER,3,0);bool released=!index&&!icons&&!index_thread.joinable()&&entries.empty()&&displayed.empty()&&!indexed;DestroyWindow(window);if(!lightweight||!released)return 0;}return 1;
}
}
LauncherUiApi const* launcher_ui_api() { static LauncherUiApi const api{LB_LAUNCHER_UI_ABI,sizeof(LauncherUiApi),service_open,service_close,service_state,service_query,service_icon,service_activate,service_tests};return &api; }
void launcher_show(HWND) { if(lb::frontend::launcher_show())return;ensure_window(true);if(window){if(dirty)begin_index();refresh();ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);SetFocus(search);} }
void launcher_shutdown() { if(window) DestroyWindow(window);else { watcher.stop();end_index(); }end_icons();entries.clear();displayed.clear();indexed=false;dirty=false;client_open=false; }
