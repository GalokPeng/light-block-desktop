#include "lb.h"
#include "taskbar.hpp"
#include "organizer.hpp"
#include "recording.hpp"
#include "recording_audio.hpp"
#include "annotation.hpp"
#include "thumbnails.hpp"
#include "scroll_capture.hpp"
#include "pointer.hpp"
#include "json.h"
#include "network.h"
#include "clipboard.hpp"
#include "file_tools.hpp"
#include "focus.hpp"
#include "particles.hpp"
#include "frontend.hpp"
#include "migration.hpp"
#include "profile.hpp"
#include "static_wallpaper.hpp"
#include "performance.hpp"
#include "capture_gate.hpp"
#include <commdlg.h>
#include <map>

struct Hit { RECT rect;int id; };
static Hit hits[180];static UINT hit_count;static UINT taskbar_created;static LONG cancel_import;static UINT grid_offset,hidden_builtins;
static wchar_t removing[LB_PATH];static ULONGLONG remove_started;
static bool desktop_menu_active{},fixture_mode{};
static lb::capture::StartGate capture_gate;
static BOOL capture_previous_transitions=FALSE;
struct Import { wchar_t source[LB_PATH],dest[LB_PATH],sha1[41];HBITMAP thumb;BOOL ok;DWORD error;int progress;bool apply; };
static int px(int n) { return MulDiv(n,(int)g_app.dpi,96); }
static COLORREF bg(void) { return g_app.dark?RGB(18,23,32):RGB(246,247,250); }
static COLORREF panel(void) { return g_app.dark?RGB(26,33,45):RGB(255,255,255); }
static COLORREF fg(void) { return g_app.dark?RGB(235,240,249):RGB(29,38,49); }
static COLORREF muted(void) { return g_app.dark?RGB(144,158,180):RGB(105,117,135); }
static COLORREF edge(void) { return g_app.dark?RGB(48,59,76):RGB(223,229,237); }
static void add_hit(RECT r,int id) { if(hit_count<ARRAYSIZE(hits)) hits[hit_count++]=Hit{r,id}; }
static int hit_test(POINT p) { for(UINT n=hit_count;n>0;--n) if(PtInRect(&hits[n-1].rect,p)) return hits[n-1].id;return 0; }
static void button(HDC dc,RECT r,int id,const wchar_t *text,BOOL primary,BOOL active) {
    COLORREF color=primary?RGB(112,217,185):(active?RGB(49,81,77):panel());
    if(g_app.hovered==id && !primary) color=g_app.dark?RGB(43,54,70):RGB(232,239,246);
    rounded(dc,r,px(12),color,primary?color:edge());
    draw_text(dc,text,r,g_app.font,primary?RGB(13,42,36):active?RGB(226,248,239):fg(),DT_CENTER|DT_VCENTER|DT_SINGLELINE);add_hit(r,id);
}
static void fonts(void) {
    struct FontSet {
        lb::unique_gdi<HFONT> body,small,heading,title;
        explicit FontSet(UINT dpi) {
            auto create=[dpi](int size,int weight) { return CreateFontW(-MulDiv(size,int(dpi),96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI"); };
            body.reset(create(14,FW_NORMAL));small.reset(create(12,FW_NORMAL));heading.reset(create(21,FW_SEMIBOLD));title.reset(create(29,FW_SEMIBOLD));
        }
    };
    /* Editors borrow these fonts. Keep previous DPI sets alive until process exit. */
    static std::map<UINT,std::unique_ptr<FontSet>> cache;
    UINT dpi=std::clamp(g_app.dpi,72u,768u);auto found=cache.find(dpi);
    if(found==cache.end()) {
        if(cache.size()<16) found=cache.emplace(dpi,std::make_unique<FontSet>(dpi)).first;
        else found=std::min_element(cache.begin(),cache.end(),[dpi](auto const& a,auto const& b){return abs(int(a.first)-int(dpi))<abs(int(b.first)-int(dpi));});
    }
    auto &set=*found->second;HFONT fallback=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g_app.font=set.body?set.body.get():fallback;g_app.small=set.small?set.small.get():fallback;g_app.heading=set.heading?set.heading.get():fallback;g_app.title=set.title?set.title.get():fallback;
}
static void bitmap_draw(HDC dc,HBITMAP bitmap,RECT r,BOOL fill) {
    if(!bitmap) return;BITMAP info;GetObjectW(bitmap,sizeof info,&info);RECT dest=fit_rect(r,(UINT)info.bmWidth,(UINT)info.bmHeight,fill);
    int save=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);HDC memory=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(memory,bitmap);SetStretchBltMode(dc,HALFTONE);
    StretchBlt(dc,dest.left,dest.top,dest.right-dest.left,dest.bottom-dest.top,memory,0,0,info.bmWidth,info.bmHeight,SRCCOPY);SelectObject(memory,old);DeleteDC(memory);RestoreDC(dc,save);
}
static const wchar_t *item_title(UINT index) {
    static const wchar_t *zh[]={L"夜驰 · 城郊高速",L"银河 · 高山湖泊",L"山海 · 晨光海湾",L"海岸 · 雨夜弯道",L"流光 · GIF",L"律动 · 视频"};
    static const wchar_t *en[]={L"Night drive",L"Milky Way",L"Mountain bay",L"Rain coast",L"Star trail · GIF",L"Motion · Video"};
    int builtin=g_app.items[index].builtin;return builtin>=0&&builtin<6?(g_app.english?en[builtin]:zh[builtin]):g_app.items[index].title;
}
static const wchar_t *page_title() {
    const wchar_t *zh[]={L"让桌面，轻一点",L"随手可用的工具",L"关于轻块桌面",L"美化",L"设置",L"截屏"};
    const wchar_t *en[]={L"A lighter desktop",L"Tools",L"About Light Block",L"Appearance",L"Settings",L"Capture"};
    return tr(zh[std::min(g_app.page,5u)],en[std::min(g_app.page,5u)]);
}
static void paint(HWND window) {
    PAINTSTRUCT ps;HDC screen=BeginPaint(window,&ps);RECT r;GetClientRect(window,&r);if(r.right<=0 || r.bottom<=0) { EndPaint(window,&ps);return; }
    HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);HGDIOBJ old=SelectObject(dc,bitmap);HBRUSH brush=CreateSolidBrush(bg());FillRect(dc,&r,brush);DeleteObject(brush);hit_count=0;
    int side=px(202),left=side+px(34),right=r.right-px(34),width=right-left;
    rounded(dc,RECT{px(24),px(30),px(62),px(68)},px(12),RGB(112,217,185),RGB(112,217,185));
    draw_text(dc,L"L",RECT{px(24),px(30),px(62),px(68)},g_app.heading,RGB(14,44,36),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    draw_text(dc,tr(L"轻块桌面",L"Light Block"),RECT{px(74),px(30),side-px(8),px(57)},g_app.font,fg(),DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    button(dc,RECT{px(18),px(120),side-px(16),px(165)},100,tr(L"▧   壁纸",L"▧   Wallpapers"),FALSE,g_app.page==0);
    button(dc,RECT{px(18),px(176),side-px(16),px(221)},103,tr(L"◇   美化",L"◇   Appearance"),FALSE,g_app.page==3);
    button(dc,RECT{px(18),px(232),side-px(16),px(277)},101,tr(L"◇   工具",L"◇   Tools"),FALSE,g_app.page==1);
    button(dc,RECT{px(18),px(288),side-px(16),px(333)},104,tr(L"▣   截屏",L"▣   Capture"),FALSE,g_app.page==5);
    button(dc,RECT{px(18),px(344),side-px(16),px(389)},105,tr(L"⚙   设置",L"⚙   Settings"),FALSE,g_app.page==4);
    button(dc,RECT{px(18),px(400),side-px(16),px(445)},102,tr(L"ⓘ   关于",L"ⓘ   About"),FALSE,g_app.page==2);
    button(dc,RECT{right-px(92),px(28),right-px(50),px(66)},601,g_app.dark?L"☀":L"☾",FALSE,FALSE);
    button(dc,RECT{right-px(42),px(28),right,px(66)},602,g_app.english?L"中":L"EN",FALSE,FALSE);
    draw_text(dc,page_title(),RECT{left,px(28),right-px(115),px(75)},g_app.title,fg(),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    draw_text(dc,g_app.page==0?tr(L"原画质播放 · 独立进程 · 默认静音",L"Original files · Isolated playback · Muted by default"):tr(L"轻块桌面",L"Light Block Desktop"),RECT{left,px(88),right,px(112)},g_app.font,muted(),DT_LEFT|DT_SINGLELINE);
    if(g_app.page==0) {
        int third=px(88);
        button(dc,RECT{left,px(133),left+third,px(171)},300,tr(L"静态",L"Images"),FALSE,g_app.filter==0);
        button(dc,RECT{left+third+px(8),px(133),left+third*2+px(8),px(171)},301,L"GIF",FALSE,g_app.filter==1);
        button(dc,RECT{left+third*2+px(16),px(133),left+third*3+px(16),px(171)},302,tr(L"视频",L"Video"),FALSE,g_app.filter==2);
        button(dc,RECT{right-px(116),px(133),right,px(171)},600,tr(L"＋ 添加文件",L"＋ Add file"),FALSE,FALSE);
        button(dc,RECT{right-px(258),px(133),right-px(128),px(171)},706,tr(L"在线壁纸",L"Online wallpapers"),FALSE,FALSE);
        int columns=2,gap=px(18),card_w=(width-gap)/columns,top=px(194),card_h=px(170),shown=0;
        int max_rows=LB_MAX(1,(r.bottom-px(196)-top+gap)/(card_h+gap));
        UINT matching=0;for(UINT n=0;n<g_app.count;++n) if((UINT)g_app.items[n].kind==g_app.filter) ++matching;
        UINT capacity=(UINT)(max_rows*columns);if(grid_offset>=matching) grid_offset=matching>capacity?matching-capacity:0;
        UINT skipped=0;
        for(UINT n=0;n<g_app.count;++n) {
            if((UINT)g_app.items[n].kind!=g_app.filter) continue;
            if(skipped++<grid_offset) continue;
            if(shown>=max_rows*columns) break;
            int col=shown%columns,row=shown/columns;RECT card={left+col*(card_w+gap),top+row*(card_h+gap),left+col*(card_w+gap)+card_w,top+row*(card_h+gap)+card_h};
            BOOL selected=g_app.selected==n;rounded(dc,card,px(16),panel(),selected?RGB(112,217,185):edge());
            RECT picture={card.left+px(8),card.top+px(8),card.right-px(8),card.bottom-px(41)};
            if(g_app.items[n].thumb) bitmap_draw(dc,g_app.items[n].thumb,picture,TRUE);
            else {
                rounded(dc,picture,px(12),g_app.dark?RGB(32,47,57):RGB(230,242,238),edge());
                draw_text(dc,L"▶",picture,g_app.title,RGB(112,217,185),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            draw_text(dc,item_title(n),RECT{card.left+px(13),card.bottom-px(35),card.right-px(10),card.bottom-px(8)},g_app.font,fg(),DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            add_hit(card,20000+(int)n);++shown;
        }
        if(!shown) draw_text(dc,tr(L"添加本地文件，开始预览",L"Add a local file to preview"),RECT{left,px(220),right,px(350)},g_app.heading,muted(),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        int bottom=r.bottom-px(122);
        button(dc,RECT{left,bottom,left+px(140),bottom+px(44)},500,tr(L"▶  预览",L"▶  Preview"),TRUE,FALSE);
        button(dc,RECT{left+px(152),bottom,left+px(304),bottom+px(44)},501,tr(L"应用到桌面",L"Play on desktop"),FALSE,FALSE);
        button(dc,RECT{left+px(316),bottom,left+px(396),bottom+px(44)},502,tr(L"停止",L"Stop"),FALSE,FALSE);
        button(dc,RECT{right-px(168),bottom,right-px(92),bottom+px(44)},503,g_app.paused?tr(L"继续",L"Resume"):tr(L"暂停",L"Pause"),FALSE,FALSE);
        button(dc,RECT{right-px(80),bottom,right,bottom+px(44)},504,g_app.muted?tr(L"静音",L"Muted"):tr(L"有声",L"Audio"),FALSE,FALSE);
        draw_text(dc,g_app.status,RECT{left,r.bottom-px(61),right,r.bottom-px(19)},g_app.small,muted(),DT_LEFT|DT_VCENTER|DT_WORDBREAK);
    } else if(g_app.page==1) {
        const wchar_t *titles[]={tr(L"快速启动",L"Quick launcher"),tr(L"剪贴板历史",L"Clipboard history"),tr(L"便签与任务",L"Notes and tasks"),tr(L"桌面便签",L"Desktop note"),tr(L"文件工作台",L"File workbench"),tr(L"专注与工作区",L"Focus and workspaces")};
        const wchar_t *descriptions[]={tr(L"搜索应用、桌面内容和工具，或输入算式。",L"Find apps, desktop items and tools, or calculate."),tr(L"文字与图片历史、搜索、固定和再次复制。",L"Text and image history, search, pins and copy."),tr(L"全文搜索、完成状态、归档和到期提醒。",L"Search, completion, archive and reminders."),tr(L"新建可调整大小、自选颜色的桌面便签。",L"Create a resizable desktop note with your own color."),tr(L"预览、摘要、批量重命名、撤销和中断恢复。",L"Preview, hash, batch rename, undo and recovery."),tr(L"专注计时、壁纸暂停与工作区布局。",L"Focus timer, wallpaper pause and workspace layouts.")};
        const int actions[]={702,707,705,700,708,709};int gap=px(16),card_width=(width-gap)/2;
        for(int n=0;n<6;++n) {
            int x=left+(n%2)*(card_width+gap),y=px(142+(n/2)*168);RECT card{x,y,x+card_width,y+px(152)};rounded(dc,card,px(18),panel(),edge());
            draw_text(dc,titles[n],RECT{x+px(20),y+px(16),x+card_width-px(18),y+px(50)},g_app.heading,fg(),DT_LEFT|DT_SINGLELINE);
            draw_text(dc,descriptions[n],RECT{x+px(20),y+px(56),x+card_width-px(18),y+px(100)},g_app.font,muted(),DT_LEFT|DT_WORDBREAK);
            button(dc,RECT{x+px(20),y+px(106),x+px(132),y+px(140)},actions[n],tr(L"打开",L"Open"),FALSE,FALSE);
        }
    } else if(g_app.page==2) {
        rounded(dc,RECT{left,px(147),right,px(487)},px(20),panel(),edge());
        draw_text(dc,tr(L"轻块桌面",L"Light Block Desktop"),RECT{left+px(26),px(176),right-px(26),px(216)},g_app.heading,fg(),DT_LEFT|DT_SINGLELINE);
        wchar_t text[1400];StringCchPrintfW(text,1400,tr(L"版本 %s\n\n纯属个人娱乐，有 BUG 随便提 issues，自己用得不顺就改改。",L"Version %s\n\nMade for personal enjoyment. Report bugs in Issues or change what does not suit you."),LB_VERSION);
        draw_text(dc,text,RECT{left+px(26),px(237),right-px(26),px(473)},g_app.font,muted(),DT_LEFT|DT_WORDBREAK);
        button(dc,RECT{left,px(514),left+px(176),px(556)},704,tr(L"打开数据目录",L"Open data folder"),FALSE,FALSE);
    }
    BitBlt(screen,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window,&ps);
}
static void add_item(const wchar_t *path,const wchar_t *title,HBITMAP thumbnail,int builtin=-1) {
    if(g_app.count>=LB_MAX_ITEMS) { if(thumbnail) DeleteObject(thumbnail);return; }
    g_app.items.emplace_back();Item *item=&g_app.items.back();
    StringCchCopyW(item->path,LB_PATH,path);StringCchCopyW(item->title,160,title);item->kind=media_kind(path);item->thumb=thumbnail;item->builtin=builtin;
    if(!thumbnail&&g_app.window) lb::thumbnails::request(path);
    ++g_app.count;++g_app.library_revision;
}
static void load_library(void) {
    Json library;wchar_t index[LB_PATH];path_join(index,LB_PATH,g_app.data,L"library.json");
    if(json_file(&library,index,4096)) hidden_builtins=UINT(json_integer(&library,json_property(&library,0,"hidden_builtins"),0))&63;
    const wchar_t *files[]={L"night-drive.jpg",L"milky-way.jpg",L"mountain-bay.jpg",L"rain-coast.jpg",L"startrail.gif",L"motion.mp4"};
    wchar_t assets[LB_PATH],path[LB_PATH];path_join(assets,LB_PATH,g_app.directory,L"assets");
    for(UINT n=0;n<ARRAYSIZE(files);++n) { path_join(path,LB_PATH,assets,files[n]);if(!(hidden_builtins&(1u<<n))&&GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) add_item(path,files[n],NULL,int(n)); }
    wchar_t directory[LB_PATH],pattern[LB_PATH];path_join(directory,LB_PATH,g_app.data,L"media");path_join(pattern,LB_PATH,directory,L"*");WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);
    if(find!=INVALID_HANDLE_VALUE) { do { if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || media_kind(data.cFileName)==MEDIA_UNKNOWN) continue;path_join(path,LB_PATH,directory,data.cFileName);add_item(path,data.cFileName,NULL); }while(FindNextFileW(find,&data));FindClose(find); }
    Json migrated;path_join(index,LB_PATH,g_app.data,L"migrated-library.json");if(json_file(&migrated,index,16*1024*1024)){int entries=json_property(&migrated,0,"entries");if(entries>=0&&migrated.tokens[entries].type==JSON_ARRAY){std::map<std::wstring,std::wstring> titles;for(int at=entries+1;at<migrated.tokens[entries].next;at=migrated.tokens[at].next){wchar_t path_value[LB_PATH],title[160];if(json_string(&migrated,json_property(&migrated,at,"path"),path_value,LB_PATH)&&json_string(&migrated,json_property(&migrated,at,"title"),title,160))titles[path_value]=title;}for(auto& item:g_app.items)if(auto it=titles.find(item.path);it!=titles.end())StringCchCopyW(item.title,160,it->second.c_str());}}
}
static bool save_library(void) {
    wchar_t path[LB_PATH],json[100];path_join(path,LB_PATH,g_app.data,L"library.json");StringCchPrintfW(json,100,L"{\"schema\":1,\"hidden_builtins\":%u}\n",hidden_builtins);return write_utf8_atomic(path,json)!=FALSE;
}
static void erase_item(UINT index) {
    if(index>=g_app.count) return;if(g_app.items[index].thumb) DeleteObject(g_app.items[index].thumb);
    g_app.items.erase(g_app.items.begin()+index);--g_app.count;++g_app.library_revision;
    if(index<g_app.selected) --g_app.selected;g_app.selected=g_app.count?std::min(g_app.selected,g_app.count-1):0;grid_offset=0;InvalidateRect(g_app.window,nullptr,FALSE);
}
static void remove_tick(void) {
    if(!*removing){KillTimer(g_app.window,19);return;}
    auto finish=[] {player_cancel_delete(removing);removing[0]=0;KillTimer(g_app.window,19);};
    int released=player_file_released(removing);
    if(released<0||GetTickCount64()-remove_started>65000){finish();set_status(L"壁纸未能安全释放，文件与图库条目已保留，请重试",L"The wallpaper could not be released safely. Its file and library entry were retained; try again.");return;}
    if(!released)return;
    for(UINT n=0;n<g_app.count;++n)if(!_wcsicmp(g_app.items[n].path,removing)){
        if(g_app.items[n].builtin>=0){auto previous=hidden_builtins;hidden_builtins|=1u<<g_app.items[n].builtin;if(!save_library()){hidden_builtins=previous;finish();set_status(L"无法保存图库，已保留原条目",L"Could not save the library. The entry was retained.");return;}erase_item(n);finish();set_status(L"已隐藏内置壁纸，可从图库菜单恢复",L"Bundled wallpaper hidden. Restore it from the library menu.");return;}
        if(DeleteFileW(removing)||GetLastError()==ERROR_FILE_NOT_FOUND){erase_item(n);finish();set_status(L"已删除图库副本，原文件保持不变",L"Library copy removed. Your original file is unchanged.");}return;
    }
    finish();
}
static void library_menu(POINT point) {
    if(g_app.page!=0) return;POINT client=point;ScreenToClient(g_app.window,&client);int hit=hit_test(client);int index=hit>=20000&&hit<20000+int(g_app.count)?hit-20000:-1;
    HMENU menu=CreatePopupMenu();lb::scope_exit destroy([&]{DestroyMenu(menu);});
    if(index>=0) AppendMenuW(menu,MF_STRING,1,tr(L"删除壁纸",L"Delete wallpaper"));if(hidden_builtins) AppendMenuW(menu,MF_STRING,2,tr(L"恢复内置壁纸",L"Restore bundled wallpapers"));
    if(GetMenuItemCount(menu)<=0) return;int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_app.window,nullptr);
    if(action==1&&index>=0) { app_delete_item(index); }else if(action==2) { UINT previous=hidden_builtins;hidden_builtins=0;if(!save_library()) { hidden_builtins=previous;return; }for(UINT n=0;n<g_app.count;++n) if(g_app.items[n].thumb) DeleteObject(g_app.items[n].thumb);g_app.items.clear();g_app.count=0;load_library();InvalidateRect(g_app.window,nullptr,FALSE); }
}
static DWORD CALLBACK copy_progress(LARGE_INTEGER total,LARGE_INTEGER transferred,LARGE_INTEGER stream_size,LARGE_INTEGER stream_done,DWORD stream,DWORD reason,HANDLE src,HANDLE dst,LPVOID data) {
    (void)stream_size;(void)stream_done;(void)stream;(void)reason;(void)src;(void)dst;
    auto *i=static_cast<Import*>(data);int p=total.QuadPart?(int)(transferred.QuadPart*100/total.QuadPart):0;
    if(p!=i->progress) { i->progress=p;PostMessageW(g_app.window,WM_LB_IMPORT,(WPARAM)p,0); }
    return InterlockedCompareExchange(&cancel_import,0,0)?PROGRESS_CANCEL:PROGRESS_CONTINUE;
}
static DWORD WINAPI import_thread(void *data) {
    auto *i=static_cast<Import*>(data);CoInitializeEx(NULL,COINIT_MULTITHREADED);wchar_t temp[LB_PATH];StringCchPrintfW(temp,LB_PATH,L"%s.partial",i->dest);
    i->ok=CopyFileExW(i->source,temp,copy_progress,i,NULL,COPY_FILE_FAIL_IF_EXISTS);
    if(i->ok&&*i->sha1) { wchar_t actual[41];if(!hash_file(temp,L"SHA1",actual,41)||_wcsicmp(actual,i->sha1)) { i->ok=FALSE;SetLastError(ERROR_CRC); } }
    if(i->ok&&InterlockedCompareExchange(&cancel_import,0,0)) { i->ok=FALSE;SetLastError(ERROR_CANCELLED); }
    if(i->ok) i->ok=MoveFileExW(temp,i->dest,MOVEFILE_WRITE_THROUGH);
    i->error=GetLastError();if(!i->ok) DeleteFileW(temp);
    CoUninitialize();
    if(!PostMessageW(g_app.window,WM_LB_IMPORT,1001,(LPARAM)i)) { if(i->thumb) DeleteObject(i->thumb);free(i); }
    return 0;
}
static void import_file(const wchar_t *path,const wchar_t *expected=nullptr,bool apply=false) {
    if(g_app.close_requested) return;
    if(g_app.import_thread) { set_status(L"已有文件正在导入，请稍候",L"An import is already running.");return; }
    if(media_kind(path)==MEDIA_UNKNOWN) { set_status(L"请选择图片、GIF 或 Windows 支持的视频",L"Choose an image, GIF or Windows-supported video.");return; }
    if(g_app.count>=LB_MAX_ITEMS) { set_status(L"图库已达到 10000 项上限，请先整理现有内容",L"The library has reached 10000 items. Remove unused entries first.");return; }
    auto *i=static_cast<Import*>(calloc(1,sizeof(Import)));if(!i) return;
    i->apply=apply;
    if(expected&&FAILED(StringCchCopyW(i->sha1,ARRAYSIZE(i->sha1),expected))) { free(i);return; }
    StringCchCopyW(i->source,LB_PATH,path);wchar_t directory[LB_PATH],name[300];path_join(directory,LB_PATH,g_app.data,L"media");ensure_directory(directory);
    StringCchPrintfW(name,300,L"%llu-%s",GetTickCount64(),PathFindFileNameW(path));
    if(!path_join(i->dest,LB_PATH,directory,name)) { free(i);return; }
    InterlockedExchange(&cancel_import,0);g_app.import_thread=CreateThread(NULL,0,import_thread,i,0,NULL);
    if(!g_app.import_thread) free(i);else set_status(L"正在复制原始文件到图库…",L"Copying the original file into your library…");
}
static void browse_file(void) {
    wchar_t file[LB_PATH]=L"";OPENFILENAMEW dialog={sizeof dialog};dialog.hwndOwner=g_app.window;dialog.lpstrFilter=L"Images / GIF / Video\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.gif;*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.webm;*.mkv\0All files\0*.*\0\0";dialog.lpstrFile=file;dialog.nMaxFile=LB_PATH;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) import_file(file);
}
void library_import(const wchar_t *path) { import_file(path); }
void library_import_checked(const wchar_t *path,const wchar_t *sha1) { import_file(path,sha1); }
void library_import_apply_checked(const wchar_t *path,const wchar_t *sha1) { import_file(path,sha1,true); }
static void show_main(void) { if(g_app.close_requested) return; if(lb::frontend::active()) { lb::frontend::show();return; }ShowWindow(g_app.window,SW_RESTORE);SetWindowPos(g_app.window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(g_app.window); }
void app_open_settings(int page) { if(lb::frontend::active()) { lb::frontend::show(page>=1&&page<=3?1:page==4?2:4);return; }g_app.page=page>=1&&page<=3?3:page==4?5:4;settings_mount(page);lb::clipboard::layout();lb::files::layout();lb::focus::layout();show_main();InvalidateRect(g_app.window,nullptr,FALSE); }
static void tray_add(void) {
    ZeroMemory(&g_app.tray,sizeof g_app.tray);g_app.tray.cbSize=sizeof g_app.tray;g_app.tray.hWnd=g_app.window;g_app.tray.uID=1;g_app.tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;g_app.tray.uCallbackMessage=WM_LB_TRAY;
    g_app.tray.hIcon=LoadIconW(g_app.instance,MAKEINTRESOURCEW(1));if(!g_app.tray.hIcon) g_app.tray.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    StringCchCopyW(g_app.tray.szTip,ARRAYSIZE(g_app.tray.szTip),LB_PRODUCT_NAME L" / Light Block Desktop");Shell_NotifyIconW(NIM_ADD,&g_app.tray);g_app.tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&g_app.tray);
}
static void cancel_capture_start() {
    capture_gate.cancel();KillTimer(g_app.window,9);
    DwmSetWindowAttribute(g_app.window,DWMWA_TRANSITIONS_FORCEDISABLED,&capture_previous_transitions,sizeof capture_previous_transitions);
}
static void schedule_capture(bool record) {
    if(g_app.close_requested||!capture_gate.arm(GetTickCount64(),record)) return;
    log_line(L"capture preparation started recording=%d",record);
    DwmGetWindowAttribute(g_app.window,DWMWA_TRANSITIONS_FORCEDISABLED,&capture_previous_transitions,sizeof capture_previous_transitions);
    BOOL disabled=TRUE;DwmSetWindowAttribute(g_app.window,DWMWA_TRANSITIONS_FORCEDISABLED,&disabled,sizeof disabled);
    lb::frontend::hide();ShowWindow(g_app.window,SW_HIDE);
    SetTimer(g_app.window,9,32,nullptr);
}
static void capture_start_tick() {
    if(g_app.close_requested||!capture_gate.pending) {cancel_capture_start();return;}
    if(!capture_gate.due(GetTickCount64())) return;
    if(IsWindowVisible(g_app.window)||!lb::frontend::capture_hidden()) {log_line(L"capture preparation cancelled: settings/popup became visible");cancel_capture_start();return;}
    // Flush submitted composition work, yield a frame, then flush again.
    // No Sleep loop: settings/exit/cancellation keep processing messages.
    DwmFlush();
    if(!capture_gate.presented(GetTickCount64())) return;
    log_line(L"capture preparation complete elapsed=%llu ms",GetTickCount64()-capture_gate.requested);
    bool record=capture_gate.record;cancel_capture_start();
    if(!g_app.close_requested) capture_start(g_app.window,record);
}
static ULONGLONG shutdown_started{},shutdown_logged{};
static bool shutdown_finalizing{},shutdown_polling{};
static std::wstring shutdown_pending;
static bool prepare_shutdown(void) {
    std::wstring pending;
    auto collect=[&](wchar_t const* name,bool done){if(!done){if(!pending.empty()) pending+=L", ";pending+=name;}};
    collect(L"download",online_shutdown());collect(L"taskbar",lb::taskbar::shutdown());collect(L"organizer",lb::organizer::shutdown());
    collect(L"recording",lb::recording::shutdown());collect(L"thumbnails",lb::thumbnails::shutdown());collect(L"scroll-capture",lb::scroll::shutdown());
    collect(L"pointer",lb::pointer::shutdown());collect(L"clipboard",lb::clipboard::shutdown());collect(L"files",lb::files::shutdown());
    if(g_app.import_thread&&WaitForSingleObject(g_app.import_thread,0)!=WAIT_OBJECT_0) collect(L"file-import",false);
    auto now=GetTickCount64();
    if(pending!=shutdown_pending||(!pending.empty()&&now-shutdown_logged>=3000)) {
        log_line(L"shutdown pending=[%s] elapsed=%llu ms",pending.c_str(),now-shutdown_started);
        shutdown_logged=now;shutdown_pending=pending;
    }
    return pending.empty();
}
static bool exit_checkpoint(bool closing) {
    // Flush debounced settings and every note before shutting down any owner.
    if(lb::frontend::prepare_exit(closing)&&notes_flush()&&save_config()) return true;
    lb::frontend::update_exit_failed();
    set_status(L"设置或便签未能保存，已取消退出。请检查数据目录后重试。",L"Settings or notes could not be saved. Exit was cancelled; check the data folder and retry.");
    log_line(L"shutdown checkpoint failed; application kept running");return false;
}
static void request_exit(bool session_ending=false) {
    if(shutdown_polling||shutdown_finalizing) return;
    shutdown_polling=true;lb::scope_exit leave([]{shutdown_polling=false;});
    if(!g_app.close_requested) {
        if(!exit_checkpoint(true)&&!session_ending) return;
        if(!notes_shutdown()&&!session_ending) {lb::frontend::update_exit_failed();return;}
        g_app.close_requested=TRUE;shutdown_started=GetTickCount64();shutdown_logged=0;shutdown_pending.clear();
        log_line(L"shutdown requested session=%d",session_ending);
        InterlockedExchange(&cancel_import,1);
        cancel_capture_start();KillTimer(g_app.window,22);
        lb::particles::shutdown();lb::focus::shutdown();capture_shutdown();launcher_shutdown();settings_close();
        player_stop(TRUE);
        SetTimer(g_app.window,12,100,nullptr);
        set_status(L"正在结束后台任务并恢复桌面…",L"Finishing background tasks and restoring the desktop…");
    }
    if(!prepare_shutdown()) return;
    KillTimer(g_app.window,12);shutdown_finalizing=true;
    g_app.player.quitting=TRUE;player_stop(TRUE);
    // player_tick also drains recording preview players, not just wallpaper.
    SetTimer(g_app.window,1,80,nullptr);
}
int app_session_end(int phase) {
    if(phase<0) {
        bool ready=g_app.close_requested||exit_checkpoint(false);
        if(!ready) ShutdownBlockReasonCreate(g_app.window,tr(L"正在保存便签和设置",L"Saving notes and settings"));
        return ready;
    }
    ShutdownBlockReasonDestroy(g_app.window);
    if(!phase) {log_line(L"Windows session end cancelled; application remains running");return 0;}
    // Windows may terminate us as soon as WM_ENDSESSION returns. Give helpers
    // a bounded chance to restore; durable receipts cover forced termination.
    static bool ending=false;if(ending) return 0;ending=true;
    ULONGLONG deadline=GetTickCount64()+3000;request_exit(true);
    while(IsWindow(g_app.window)&&GetTickCount64()<deadline) {
        request_exit(true);MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) {PostQuitMessage(int(message.wParam));ending=false;return 0;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(IsWindow(g_app.window)) MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if(IsWindow(g_app.window)) log_line(L"session cleanup deadline reached; recovery receipts retained, pending=[%s]",shutdown_pending.c_str());
    ending=false;return 0;
}
static void command(int id) {
    if(g_app.close_requested&&id!=900) return;
    if(lb::frontend::active()&&id>=100&&id<=105) { int pages[]={0,3,5,1,2,4};lb::frontend::show(pages[id-100]);return; }
    if(id>=100 && id<=102) { g_app.page=(UINT)id-100;if(id==101) { lb::clipboard::home();lb::files::home();lb::focus::home(); } }
    else if(id>=300 && id<=302) { g_app.filter=(UINT)id-300;grid_offset=0;for(UINT n=0;n<g_app.count;++n) if((UINT)g_app.items[n].kind==g_app.filter) { g_app.selected=n;break; } }
    else if(id>=20000 && id<20000+(int)g_app.count) g_app.selected=(UINT)id-20000;
    else { switch(id) {
    case 103:settings_show(1);break;
    case 104:settings_show(4);break;
    case 105:settings_show(0);break;
    case 500: { if(g_app.selected<g_app.count&&_wcsicmp(g_app.items[g_app.selected].path,removing)) player_request(g_app.items[g_app.selected].path,FALSE);break; }
    case 501: { if(g_app.selected<g_app.count&&_wcsicmp(g_app.items[g_app.selected].path,removing)) player_request(g_app.items[g_app.selected].path,TRUE);break; }
    case 502:player_stop(TRUE);g_app.settings.active_path[0]=0;save_config();break;
    case 505:player_restore_wallpaper();break;
    case 507:if(desktop_menu_active)lb::profile::desktop_menu(g_app.exe,true,true,g_app.english!=FALSE,g_app.data,!g_app.settings.wallpaper_sound);break;
    case 506:settings_wallpaper_sound(!g_app.settings.wallpaper_sound);if(desktop_menu_active)lb::profile::desktop_menu(g_app.exe,true,true,g_app.english!=FALSE,g_app.data,!g_app.settings.wallpaper_sound);break;
    case 503:if(player_active().ready) { player_active().paused=!player_active().paused;g_app.paused=player_active().paused;player_control(1); }break;
    case 504:
        if(&player_active()==&g_app.wallpaper){command(506);break;}
        if(!player_active().ready || media_kind(player_active().pending_path)!=MEDIA_VIDEO) { set_status(L"播放视频后可开启声音",L"Start a video before enabling audio.");break; }
        if(player_active().muted && MessageBoxW(g_app.window,tr(L"开启视频声音会启用音频设备，可能增加资源占用。是否开启？",L"Enabling audio opens an audio device and may use more resources. Continue?"),L"Light Block Desktop",MB_YESNO|MB_ICONQUESTION)!=IDYES) break;
        player_active().muted=!player_active().muted;g_app.muted=player_active().muted;player_control(2);break;
    case 600:browse_file();break;
    case 601:g_app.dark=!g_app.dark;theme_window(g_app.window);settings_refresh();save_config();break;
    case 602:g_app.english=!g_app.english;if(desktop_menu_active)lb::profile::desktop_menu(g_app.exe,true,true,g_app.english!=FALSE,g_app.data,!g_app.settings.wallpaper_sound);g_app.language=g_app.english?2:1;settings_refresh();save_config();set_status(L"选择壁纸，预览或应用到桌面",L"Choose a wallpaper to preview or apply to your desktop.");break;
    case 700:note_create(g_app.window);break;
    case 701:schedule_capture(false);break;
    case 702:launcher_show(g_app.window);break;
    case 703: { wchar_t path[LB_PATH];path_join(path,LB_PATH,g_app.data,L"notes");ensure_directory(path);ShellExecuteW(g_app.window,L"open",path,NULL,NULL,SW_SHOWNORMAL);break; }
    case 704:ShellExecuteW(g_app.window,L"open",g_app.data,NULL,NULL,SW_SHOWNORMAL);break;
    case 705:notes_manager();break;
    case 706:if(!lb::frontend::show_tool(706))online_show();break;
    case 707:if(lb::frontend::show_tool(707))break;lb::files::home();lb::focus::home();lb::clipboard::show();break;
    case 708:if(lb::frontend::show_tool(708))break;lb::clipboard::home();lb::focus::home();lb::files::show();break;
    case 709:if(lb::frontend::show_tool(709))break;lb::clipboard::home();lb::files::home();lb::focus::show();break;
    case 710:if(!lb::frontend::show_tool(710))lb::recording::open_file(g_app.window);break;
    case 711:lb::capture::edit_clipboard(g_app.window);break;
    case 712:lb::recording::toggle(g_app.window);break;
    case 713:schedule_capture(true);break;
    case 900:request_exit();return;
    case 901:show_main();return;
    } }
    settings_layout();lb::clipboard::layout();lb::files::layout();lb::focus::layout();InvalidateRect(g_app.window,NULL,FALSE);
}
void app_command(int id) { command(id); }
void app_delete_item(int index) {
    if(index<0||index>=int(g_app.count)||*removing)return;auto const& item=g_app.items[index];
    if(item.builtin<0){auto parent=std::filesystem::path(item.path).parent_path(),allowed=std::filesystem::path(g_app.data)/L"media";if(_wcsicmp(parent.c_str(),allowed.c_str()))return;}
    StringCchCopyW(removing,LB_PATH,item.path);remove_started=GetTickCount64();player_forget(item.path);SetTimer(g_app.window,19,100,nullptr);remove_tick();
}
static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(online_message(message,wp,lp)) return 0;
    if(lb::clipboard::message(message,wp,lp)) return 0;
    if(lb::files::message(message,wp,lp)) return 0;
    if(message!=WM_LB_READY&&lb::focus::message(message,wp,lp)) return 0;
    if(lb::organizer::message(message,wp,lp)) return 0;
    if(taskbar_created && message==taskbar_created) { if(g_app.close_requested) return 0;tray_add();lb::organizer::shell_restarted();return 0; }
    switch(message) {
    case WM_CREATE:g_app.window=window;SetPropW(window,L"LB.Profile",(HANDLE)(uintptr_t)(DWORD)(uintptr_t)((CREATESTRUCTW*)lp)->lpCreateParams);if(!fixture_mode)tray_add();DragAcceptFiles(window,TRUE);if(!fixture_mode&&!configure_hotkey((UINT)g_app.settings.capture_modifiers,(UINT)g_app.settings.capture_key)) log_line(L"capture hotkey unavailable: %lu",GetLastError());if(!fixture_mode&&!configure_tool_hotkeys(g_app.settings.hotkeys_enabled!=0,false))log_line(L"Tool shortcuts unavailable: %lu",GetLastError());return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint(window);return 0;
    case WM_SIZE:settings_layout();lb::clipboard::layout();lb::files::layout();lb::focus::layout();InvalidateRect(window,NULL,FALSE);return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize=POINT{px(990),px(700)};return 0;
    case WM_DPICHANGED:g_app.dpi=HIWORD(wp);fonts(); { RECT *r=(RECT*)lp;SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); }return 0;
    case WM_LBUTTONUP:command(hit_test(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}));return 0;
    case WM_CONTEXTMENU: { POINT point;GetCursorPos(&point);library_menu(point);return 0; }
    case WM_MOUSEMOVE: { int hovered=hit_test(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hovered!=g_app.hovered) { g_app.hovered=hovered;InvalidateRect(window,NULL,FALSE); }TRACKMOUSEEVENT t={sizeof t,TME_LEAVE,window,0};TrackMouseEvent(&t);return 0; }
    case WM_MOUSELEAVE:g_app.hovered=0;InvalidateRect(window,NULL,FALSE);return 0;
    case WM_MOUSEWHEEL:
        if(g_app.page==0) { if(GET_WHEEL_DELTA_WPARAM(wp)<0) { UINT matching=0;for(UINT n=0;n<g_app.count;++n) if((UINT)g_app.items[n].kind==g_app.filter) ++matching;if(grid_offset+2<matching) grid_offset+=2; }else grid_offset=grid_offset>1?grid_offset-2:0;InvalidateRect(window,NULL,FALSE); }return 0;
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT && g_app.hovered) { SetCursor(LoadCursorW(NULL,IDC_HAND));return TRUE; }break;
    case WM_COMMAND:command(LOWORD(wp));return 0;
    case WM_HOTKEY:if(wp==1||wp==2)command(701);else if(wp==11&&tool_hotkey_registered(11))command(712);else if(wp==12&&tool_hotkey_registered(12))command(702);else if(wp==13&&tool_hotkey_registered(13))command(707);return 0;
    case WM_KEYDOWN:if(g_app.page==0) { if(wp==VK_SPACE) command(503);else if(wp==VK_RETURN) command(500); }return 0;
    case WM_TIMER:if(wp==1) player_tick();else if(wp==9) capture_start_tick();else if(wp==12&&g_app.close_requested) request_exit();else if(wp==13) stress_tick();else if(wp==14) notes_tick();else if(wp==16) lb::taskbar::tick();else if(wp==17) lb::organizer::tick(UINT(wp));else if(wp==18) lb::recording::tick();else if(wp==19) remove_tick();else if(wp==20) lb::pointer::tick();else if(wp==23) lb::performance::tick();return 0;
    case WM_APP+98:lb::thumbnails::receive();return 0;
    case WM_LB_HELLO:case WM_LB_READY:case WM_LB_ERROR:case WM_LB_POSITION:case WM_LB_DURATION:case WM_LB_STATE:
        player_message(message,wp,lp);if(message==WM_LB_STATE&&desktop_menu_active)lb::profile::desktop_menu(g_app.exe,true,true,g_app.english!=FALSE,g_app.data,!g_app.settings.wallpaper_sound);if(message==WM_LB_READY) lb::focus::message(message,wp,lp);SetPropW(window,L"LB.PlayerPID",(HANDLE)(uintptr_t)g_app.player.pid);SetPropW(window,L"LB.Ready",(HANDLE)(uintptr_t)g_app.player.ready);return 0;
    case WM_LB_IMPORT:
        if(wp==1001) { Import *i=(Import*)lp;if(g_app.import_thread) { CloseHandle(g_app.import_thread);g_app.import_thread=NULL; }if(i->ok) { add_item(i->dest,PathFindFileNameW(i->source),i->thumb);g_app.selected=g_app.count-1;g_app.filter=g_app.items[g_app.selected].kind;set_status(L"原始文件已复制到图库",L"Original file copied into your library.");if(i->apply&&!g_app.close_requested)player_request(i->dest,TRUE); }else { if(i->thumb) DeleteObject(i->thumb);if(!g_app.close_requested) error_box(window,tr(L"文件导入失败",L"File import failed"),HRESULT_FROM_WIN32(i->error)); }free(i); }
        else { StringCchPrintfW(g_app.status,240,tr(L"正在复制原始文件… %u%%",L"Copying original file… %u%%"),(UINT)wp);InvalidateRect(window,NULL,FALSE); }return 0;
    case WM_DROPFILES: { wchar_t path[LB_PATH];if(DragQueryFileW((HDROP)wp,0,path,LB_PATH)) import_file(path);DragFinish((HDROP)wp);return 0; }
    case WM_LB_TRAY: {
        UINT event=LOWORD(lp);if(event==NIN_SELECT || event==NIN_KEYSELECT || event==WM_LBUTTONUP) show_main();
        else if(event==WM_CONTEXTMENU || event==WM_RBUTTONUP) { HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,901,tr(L"打开设置",L"Open settings"));AppendMenuW(menu,MF_STRING,700,tr(L"新建便签",L"New note"));AppendMenuW(menu,MF_STRING,712,lb::recording::active()?tr(L"结束录屏",L"Stop recording"):tr(L"开始录屏",L"Start recording"));AppendMenuW(menu,MF_STRING,502,tr(L"停止壁纸",L"Stop wallpaper"));AppendMenuW(menu,MF_STRING,505,tr(L"恢复系统壁纸",L"Restore original wallpaper"));AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,900,tr(L"退出轻块桌面",L"Quit Light Block Desktop"));POINT p;GetCursorPos(&p);SetForegroundWindow(window);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,NULL);DestroyMenu(menu);if(id) command(id);PostMessageW(window,WM_NULL,0,0); }return 0;
    }
    case WM_QUERYENDSESSION:return app_session_end(-1);
    case WM_ENDSESSION:return app_session_end(wp?1:0);
    case WM_CLOSE:ShowWindow(window,SW_HIDE);return 0;
    case WM_DESTROY:log_line(L"shutdown complete elapsed=%llu ms",shutdown_started?GetTickCount64()-shutdown_started:0);configure_tool_hotkeys(false,false);Shell_NotifyIconW(NIM_DELETE,&g_app.tray);UnregisterHotKey(window,1);UnregisterHotKey(window,2);player_dispose();save_config();RemovePropW(window,L"LB.Profile");RemovePropW(window,L"LB.PlayerPID");RemovePropW(window,L"LB.Ready");PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
struct Existing { DWORD hash;HWND window; };
static BOOL CALLBACK find_existing(HWND window,LPARAM value) {
    Existing *e=(Existing*)value;wchar_t name[100];GetClassNameW(window,name,100);
    if(!wcscmp(name,LB_CLASS) && (DWORD)(uintptr_t)GetPropW(window,L"LB.Profile")==e->hash) { e->window=window;return FALSE; }return TRUE;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR line,int show) {
    (void)previous;(void)line;g_app.instance=instance;
    typedef BOOL (WINAPI *DpiFn)(DPI_AWARENESS_CONTEXT);DpiFn dpi=(DpiFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");if(dpi) dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GetModuleFileNameW(NULL,g_app.exe,LB_PATH);StringCchCopyW(g_app.directory,LB_PATH,g_app.exe);PathRemoveFileSpecW(g_app.directory);path_join(g_app.data,LB_PATH,g_app.directory,L"data");
    int argc=0;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv) return 2;
    BOOL static_helper=FALSE,worker=FALSE,quit=FALSE,self_test=FALSE,stress=FALSE,crash=FALSE,background=FALSE,taskbar_helper=FALSE,icon_lease=FALSE,organizer_preview=FALSE,pointer_helper=FALSE,legacy_ui=FALSE,custom_profile=FALSE,lifecycle_fixture=FALSE;const wchar_t *report=NULL,*ui_snapshots=nullptr,*update_fixture=nullptr,*performance_report=nullptr;int desktop_action=0;
    for(int n=1;n<argc;++n) { if(!wcscmp(argv[n],L"--data-dir") && n+1<argc) { custom_profile=TRUE;wchar_t full[LB_PATH];DWORD length=GetFullPathNameW(argv[++n],LB_PATH,full,NULL);if(!length || length>=LB_PATH) { LocalFree(argv);return 2; }StringCchCopyW(g_app.data,LB_PATH,full); }
        else if(!wcscmp(argv[n],L"--static-wallpaper-helper")) static_helper=TRUE;else if(!wcscmp(argv[n],L"--desktop-action")&&n+1<argc) { auto value=argv[++n];desktop_action=!wcscmp(value,L"note")?700:!wcscmp(value,L"sound")?506:0;if(!desktop_action){LocalFree(argv);return 2;}background=TRUE; }else if(!wcscmp(argv[n],L"--update-flow-fixture")&&n+1<argc) update_fixture=argv[++n];else if(!wcscmp(argv[n],L"--ui-snapshots")&&n+1<argc) ui_snapshots=argv[++n];else if(!wcscmp(argv[n],L"--lifecycle-fixture")) lifecycle_fixture=TRUE;else if(!wcscmp(argv[n],L"--legacy-ui")) legacy_ui=TRUE;else if(!wcscmp(argv[n],L"--worker")) worker=TRUE;else if(!wcscmp(argv[n],L"--taskbar-helper")) taskbar_helper=TRUE;else if(!wcscmp(argv[n],L"--icon-lease")) icon_lease=TRUE;else if(!wcscmp(argv[n],L"--pointer-helper")) pointer_helper=TRUE;else if(!wcscmp(argv[n],L"--organizer-preview")) organizer_preview=TRUE;else if(!wcscmp(argv[n],L"--background")) background=TRUE;else if(!wcscmp(argv[n],L"--quit")) quit=TRUE;else if(!wcscmp(argv[n],L"--performance-test")&&n+1<argc){performance_report=argv[++n];background=TRUE;}else if(!wcscmp(argv[n],L"--self-test")) self_test=TRUE;else if(!wcscmp(argv[n],L"--stress-test")) stress=TRUE;else if(!wcscmp(argv[n],L"--crash-test")) { stress=TRUE;crash=TRUE; }else if(!wcscmp(argv[n],L"--report") && n+1<argc) report=argv[++n]; }
    bool fixture=lifecycle_fixture||organizer_preview||ui_snapshots||update_fixture||self_test||stress||performance_report;bool helper=worker||taskbar_helper||icon_lease||pointer_helper||static_helper;
    fixture_mode=fixture;
    if(fixture&&!custom_profile){LocalFree(argv);return 2;}
    if(!helper&&!fixture){auto profile=lb::profile::select_with_progress(g_app.exe,custom_profile?std::filesystem::path(g_app.data):std::filesystem::path{});if(!profile.ok){MessageBoxW(nullptr,profile.error.c_str(),L"轻块桌面",MB_ICONERROR);LocalFree(argv);return 3;}StringCchCopyW(g_app.data,LB_PATH,profile.path.c_str());}
    if(!ensure_directory(g_app.data)){MessageBoxW(nullptr,L"The data folder is not writable.",L"Light Block Desktop",MB_ICONERROR);LocalFree(argv);return 3;}
    path_join(g_app.config,LB_PATH,g_app.data,L"settings.ini");if(fixture&&GetFileAttributesW(g_app.config)!=INVALID_FILE_ATTRIBUTES){LocalFree(argv);return 2;}load_config();
    if(lifecycle_fixture||organizer_preview||ui_snapshots||update_fixture||performance_report){g_app.settings.organizer_enabled=g_app.settings.taskbar_enabled=g_app.settings.cursor_theme=g_app.settings.cursor_particles=g_app.settings.clipboard_enabled=g_app.settings.restore_on_start=g_app.settings.notes_background=g_app.settings.capture_enabled=0;}
    if(organizer_preview)g_app.settings.organizer_enabled=1;
    if(static_helper){int result=lb::static_wallpaper::helper_main(argc,argv);LocalFree(argv);return result;}
    if(worker) { int result=worker_main(argc,argv);LocalFree(argv);return result; }
    if(taskbar_helper) { int result=lb::taskbar::helper_main(argc,argv);LocalFree(argv);return result; }
    if(icon_lease) { int result=lb::organizer::lease_main(argc,argv);LocalFree(argv);return result; }
    if(pointer_helper) { int result=lb::pointer::helper_main(argc,argv);LocalFree(argv);return result; }
    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if(self_test) { int result=run_self_tests(report);CoUninitialize();LocalFree(argv);return result; }
    DWORD hash=2166136261u;for(const wchar_t *p=g_app.data;*p;++p) hash=(hash^(DWORD)towlower(*p))*16777619u;if(!hash) hash=1;
    wchar_t mutex[100];StringCchPrintfW(mutex,100,L"Local\\LightBlockDesktopNative-%08lx",hash);g_app.mutex=CreateMutexW(NULL,FALSE,mutex);BOOL exists=GetLastError()==ERROR_ALREADY_EXISTS;
    if(quit || exists) { Existing e={hash,NULL};EnumWindows(find_existing,(LPARAM)&e);if(e.window&&(quit||desktop_action||!background)) PostMessageW(e.window,WM_COMMAND,quit?900:desktop_action?desktop_action:901,0);if(g_app.mutex) CloseHandle(g_app.mutex);CoUninitialize();LocalFree(argv);return quit&&!e.window?1:0; }
    if(!fixture)lb::recording::audio::cleanup_abandoned(std::filesystem::path(g_app.data)/L"recordings");
    INITCOMMONCONTROLSEX controls={sizeof controls,ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES|ICC_BAR_CLASSES|ICC_HOTKEY_CLASS|ICC_DATE_CLASSES};InitCommonControlsEx(&controls);g_app.dpi=GetDpiForSystem();fonts();load_library();
    taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");WNDCLASSEXW cls={};cls.cbSize=sizeof cls;cls.lpfnWndProc=window_proc;cls.hInstance=instance;cls.lpszClassName=LB_CLASS;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassExW(&cls);
    HWND window=CreateWindowExW(0,LB_CLASS,tr(LB_PRODUCT_NAME,LB_PRODUCT_ENGLISH),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1090),px(800),NULL,NULL,instance,(void*)(uintptr_t)hash);
    if(!window) return 5;
    if(lifecycle_fixture) SetPropW(window,L"LB.LifecycleFixture",HANDLE(1));
    bool qt_frontend=!stress&&!legacy_ui&&lb::frontend::initialize(!background&&!ui_snapshots&&!update_fixture);
    if(!stress&&!legacy_ui&&!qt_frontend) { MessageBoxW(nullptr,tr(L"界面组件不完整，请重新安装轻块桌面。你的数据会被保留。",L"UI components are incomplete. Reinstall Light Block Desktop; your data will be preserved."),L"Light Block Desktop",MB_OK|MB_ICONERROR);DestroyWindow(window);for(UINT n=0;n<g_app.count;++n) if(g_app.items[n].thumb) DeleteObject(g_app.items[n].thumb);CloseHandle(g_app.mutex);LocalFree(argv);CoUninitialize();return 9; }
    if(!background&&!qt_frontend)for(UINT n=0;n<std::min(g_app.count,12u);++n)lb::thumbnails::request(g_app.items[n].path);theme_window(window);set_status(L"选择壁纸，预览或应用到桌面",L"Choose a wallpaper to preview or apply to your desktop.");if(performance_report)lb::performance::start(performance_report);else if(stress) stress_begin(report,crash);else if(!background&&!qt_frontend&&!ui_snapshots&&!update_fixture) { ShowWindow(window,show);ShowWindow(window,SW_RESTORE);SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(window);UpdateWindow(window); }
    if(!stress && g_app.settings.restore_on_start && *g_app.settings.active_path && GetFileAttributesW(g_app.settings.active_path)!=INVALID_FILE_ATTRIBUTES) player_request(g_app.settings.active_path,TRUE);
    if(!stress) notes_restore();
    if(!stress) lb::taskbar::configure();
    if(!stress) lb::organizer::configure(organizer_preview!=FALSE);
    if(!stress) lb::pointer::configure();
    if(!stress) lb::clipboard::configure();
    if(!stress) { lb::particles::configure();lb::focus::restore(); }
    log_line(L"app start C++23 native version=%s items=%u",LB_VERSION,g_app.count);
    if(!fixture)desktop_menu_active=lb::profile::desktop_menu(g_app.exe,true,true,g_app.english!=FALSE,g_app.data,!g_app.settings.wallpaper_sound);
    if(desktop_action)PostMessageW(window,WM_COMMAND,WPARAM(desktop_action),0);
    bool snapshot_ok=true;if(update_fixture){snapshot_ok=lb::frontend::update_tests(update_fixture);if(!qt_frontend)PostMessageW(window,WM_COMMAND,900,0);}if(ui_snapshots) { snapshot_ok=lb::frontend::snapshots(ui_snapshots);if(!qt_frontend) PostMessageW(g_app.window,WM_COMMAND,900,0); }if(qt_frontend) lb::frontend::run();else { MSG msg;while(GetMessageW(&msg,NULL,0,0)>0) { if(!settings_dialog_message(&msg)) { TranslateMessage(&msg);DispatchMessageW(&msg); } } }
    for(UINT n=0;n<g_app.count;++n) if(g_app.items[n].thumb) DeleteObject(g_app.items[n].thumb);
    if(g_app.import_thread) CloseHandle(g_app.import_thread);CloseHandle(g_app.mutex);LocalFree(argv);CoUninitialize();return snapshot_ok?0:1;
}
