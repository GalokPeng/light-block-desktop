#include "organizer.hpp"
#include "organizer_geometry.hpp"
#include "window_shape.hpp"
#include "desktop_surface.hpp"
#include "ui_motion.hpp"
#include "directory_watch.hpp"
#include "icon_cache.hpp"
#include "shell_menu.hpp"
#include "rename.hpp"
#include "network.h"
#include "desktop_layer.hpp"
#include "ui_api.h"
#include <commdlg.h>
#include <set>

namespace lb::organizer {
namespace {
constexpr UINT changed_message=WM_APP+92;
auto popup_menu=&TrackPopupMenu;
struct Tile {
    RECT bounds;
    size_t index;
    bool group;
    std::wstring identity;
    bool current(Model const& model) const {
        if(index==SIZE_MAX) return !group&&identity.empty();
        if(group) return index<model.groups.size()&&model.groups[index].id==identity;
        return index<model.entries.size()&&model.entries[index].path==identity;
    }
    bool matches_press(std::wstring const& path,std::wstring const& group_id) const {
        if(index==SIZE_MAX) return path.empty()&&group_id.empty();
        return group ? identity==group_id : identity==path;
    }
};
struct Loose { HWND window{};std::wstring path,icon_key;POINT position{},pressed{};bool dragging{},hint{true},hint_hover{},hint_pressed{}; };
struct ArrivalAnchor {
    POINT point{};ULONGLONG captured{};bool valid{};
    void capture(POINT p,ULONGLONG now){point=p;captured=now;valid=true;}
    bool recent(ULONGLONG now)const{return valid&&now>=captured&&now-captured<=30000;}
} arrival_anchor;
struct RenameJob {std::thread worker;std::atomic_bool done{};bool ok{};std::wstring source,target;};
// Cached indices belong to one committed model revision. Search retains one
// result so typing cannot grow the cache without a bound.
struct ShelfViews {
    uint64_t revision=UINT64_MAX;
    std::optional<std::vector<size_t>> by_name,by_time;
    std::wstring query,group;
    int order=-1;
    std::vector<size_t> filtered;
    using Covers=std::unordered_map<std::wstring,std::vector<size_t>>;
    std::optional<Covers> covers;
    void refresh(Model const& model){
        if(revision==model.revision)return;
        by_name.reset();by_time.reset();covers.reset();filtered.clear();order=-1;
        revision=model.revision;
    }
    std::vector<size_t> const& list(Model const& model,int sort,std::wstring_view search,std::wstring_view selected){
        refresh(model);
        if(search.empty()&&selected.empty()){
            auto& sorted=sort==1?by_time:by_name;
            if(!sorted)sorted=model.view(sort,L"");
            return *sorted;
        }
        if(sort!=order||search!=query||selected!=group){
            order=sort;query=search;group=selected;
            filtered=model.view(sort,query,group);
        }
        return filtered;
    }
    size_t pinned_count(Model const& model){
        auto const& name=list(model,0,L"",L"");
        size_t n=0;while(n<name.size()&&model.entries[name[n]].pin)++n;
        return n;
    }
    Covers const& group_covers(Model const& model){
        refresh(model);
        if(!covers){
            covers.emplace();
            for(auto index:list(model,0,L"",L"")){
                auto& cover=(*covers)[model.entries[index].group];
                if(cover.size()<4)cover.push_back(index);
            }
        }
        return *covers;
    }
};
struct DirectoryScan {
    std::atomic_bool done{},cancelled{};
    uint64_t serial{},revision{};
    std::vector<RootListing> listing;
};
struct Shelf {
    std::unique_ptr<Model> model;
    HWND window{},search{},editor{},editor_anchor{};
    bool editing_file{};std::unique_ptr<RenameJob> rename;
    std::vector<Tile> tiles;
    std::vector<std::wstring> roots;
    IconCache icons;
    ShelfViews views;
    SIZE painted{};
    bool foreground_cached{},dismiss_after_menu{},presenting_geometry{};
    uint64_t painted_revision{};
    std::vector<std::unique_ptr<Loose>> loose;
    DirectoryWatch watcher;
    uint64_t change_serial{};
    std::shared_ptr<DirectoryScan> scan,startup_scan;
    unique_handle lease,stop,ready;
    unsigned retry_seconds{};
    bool retry_pending{},recovering{};
    ULONGLONG protect_empty_until{};
    DWORD token{};
    bool session_suspended{};
    bool fixture{},preview{},layer_preview{},enabled{},expanded{},moving{},menu{},dragging{},mouse_inside{},stopping{},reduced_motion{};
    std::wstring group,editing_entry,drag_entry,drag_group,drag_origin,last_query;
    bool drag_started{},drag_groups{},drag_expanded{};
    uint64_t generation{};
    int hovered{-1},pressed_tile{-1},scroll{},height{},target{};
    POINT pressed{};
    RECT layout_work{};
    ULONGLONG animation{},opacity_tick{},change_started{};
    lb::motion::Spring height_motion,header_motion,opacity_motion;
    lb::desktop_surface::Buffer surface,content_black,content_white;
    struct Plate {int w,h,radius;COLORREF color,edge;lb::desktop_surface::Buffer surface;};
    std::list<Plate> plates;
    int search_alpha{-1};
    RECT editor_bounds{};
    HBRUSH edit_brush{};
    HHOOK outside{};UINT dpi{};HFONT font{},small{},heading{};
    struct Hover {float from{},value{},target{};};std::unordered_map<int,Hover> hovers;ULONGLONG hover_started{};
} shelf;
bool floating_preview(){return shelf.preview&&!shelf.layer_preview;}
LRESULT CALLBACK shelf_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK loose_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK edit_keys(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void schedule_collect();
void position_edits();
void paint(HWND);
bool render_frame(HWND,RECT,RECT,bool opacity_only=false,POINT* destination=nullptr);
void rebuild_loose();
void reflow();
// Shell property sheets can run on a different thread without retaining our
// icon HWND as their owner. Check only when collection is due, not on paints.
bool native_dialog_open(){bool found=false;EnumWindows([](HWND window,LPARAM value)->BOOL{DWORD pid{};GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId()||!IsWindowVisible(window))return TRUE;wchar_t name[32]{};GetClassNameW(window,name,32);if(wcscmp(name,L"#32770"))return TRUE;*reinterpret_cast<bool*>(value)=true;return FALSE;},LPARAM(&found));return found;}
bool owned_dialog(HWND window){auto popup=window?GetLastActivePopup(window):nullptr;return popup&&popup!=window&&popup!=shelf.search&&IsWindowVisible(popup);}
bool interaction_busy() { return shelf.rename||owned_dialog(shelf.window)||shelf.menu||shelf.dragging||!shelf.drag_group.empty()||!shelf.drag_entry.empty()||shelf.editor||std::any_of(shelf.loose.begin(),shelf.loose.end(),[](auto const& item){return item->dragging||item->hint_pressed||owned_dialog(item->window);}); }
struct MenuGuard {
    bool previous=shelf.menu;uint64_t generation=shelf.generation;
    MenuGuard(){shelf.menu=true;}
    ~MenuGuard(){if(generation==shelf.generation){shelf.menu=previous;schedule_collect();
        if(!previous&&std::exchange(shelf.dismiss_after_menu,false)&&shelf.expanded)
            PostMessageW(shelf.window,WM_APP+94,0,0);}}
    bool alive()const{return generation==shelf.generation&&shelf.window&&IsWindow(shelf.window)&&shelf.model&&!shelf.stopping;}
};
bool show_groups(wchar_t const* query){return shelf.drag_groups||(g_app.settings.organizer_view==2&&shelf.group.empty()&&!*query);}
int scaled(int value) { return MulDiv(value,int(shelf.dpi?shelf.dpi:GetDpiForSystem()),96); }
ShelfMetrics metrics(){return ShelfMetrics::make(g_app.settings.organizer_icon_size,int(shelf.dpi?shelf.dpi:GetDpiForSystem()));}
void shelf_fonts(){for(auto font:{shelf.font,shelf.small,shelf.heading})if(font)DeleteObject(font);auto create=[](int size,int weight){return CreateFontW(-scaled(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");};shelf.font=create(14,FW_NORMAL);shelf.small=create(12,FW_NORMAL);shelf.heading=create(16,FW_MEDIUM);if(shelf.search)SendMessageW(shelf.search,WM_SETFONT,WPARAM(shelf.font),TRUE);}

COLORREF mixed(COLORREF a,COLORREF b,int weight) { return RGB((GetRValue(a)*weight+GetRValue(b)*(255-weight))/255,(GetGValue(a)*weight+GetGValue(b)*(255-weight))/255,(GetBValue(a)*weight+GetBValue(b)*(255-weight))/255); }
COLORREF foreground(COLORREF color) { return GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114>140000?RGB(24,34,43):RGB(240,245,251); }
std::vector<std::wstring> desktop_roots() {
    std::vector<std::wstring> result;
    for(auto const& id:{FOLDERID_Desktop,FOLDERID_PublicDesktop}) {
        PWSTR path=nullptr;
        if(FAILED(SHGetKnownFolderPath(id,KF_FLAG_DONT_VERIFY,nullptr,&path))) return {};
        result.emplace_back(path);CoTaskMemFree(path);
    }
    return result;
}
int cell_size() { return metrics().cell_width; }
int row_size() { return metrics().row_height; }
int base_height() { return metrics().collapsed_height(); }
RECT monitor_work() {
    MONITORINFO info{sizeof info};GetMonitorInfoW(MonitorFromWindow(shelf.window,MONITOR_DEFAULTTOPRIMARY),&info);RECT work=info.rcWork;
    // GetWindowRect remains valid while our helper hides the bar with an empty
    // region. No synchronous Explorer IPC or system work-area writes here.
    for(auto name:{L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd"})for(HWND bar=nullptr;(bar=FindWindowExW(nullptr,bar,name,nullptr));){RECT bounds{};if(GetWindowRect(bar,&bounds))work=reserve_taskbar(work,info.rcMonitor,bounds);}
    return work;
}
int width(RECT work) { size_t count=shelf.model?(g_app.settings.organizer_view==2?shelf.model->groups.size()+shelf.views.pinned_count(*shelf.model):shelf.views.list(*shelf.model,g_app.settings.organizer_view,L"",L"").size()):10;int cells=std::clamp(int(count)+1,4,11);return std::max(1,std::min(cell_size()*cells+scaled(24),int(work.right-work.left)-scaled(32))); }
int width(){return width(monitor_work());}
bool place(int height,RECT const* known_work=nullptr) {
    if(shelf.dragging&&shelf.moving) return false;
    RECT work=known_work?*known_work:monitor_work(),bounds=shelf_bounds(work,width(work),height,g_app.settings.organizer_alignment,scaled(22),g_app.settings.organizer_custom_position!=0,g_app.settings.organizer_x,g_app.settings.organizer_y);
    RECT current{};GetWindowRect(shelf.window,&current);bool changed=!EqualRect(&current,&bounds);
    bool presented=false;
    if(changed&&known_work&&shelf.model){
        // Move, resize and supply the matching pixels in one compositor call.
        RECT client{0,0,bounds.right-bounds.left,bounds.bottom-bounds.top};POINT origin{bounds.left,bounds.top};
        shelf.presenting_geometry=true;scope_exit finish([]{shelf.presenting_geometry=false;});
        presented=render_frame(shelf.window,client,client,false,&origin);
    }
    if(changed&&!presented){if(floating_preview()||shelf.fixture)SetWindowPos(shelf.window,HWND_TOP,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE|SWP_NOREDRAW);else lb::desktop_layer::place(shelf.window,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOREDRAW);}
    shelf.height=bounds.bottom-bounds.top;shelf.layout_work=work;position_edits();
    if(changed&&!presented)InvalidateRect(shelf.window,nullptr,FALSE);
    if(!shelf.fixture&&!IsWindowVisible(shelf.window))ShowWindow(shelf.window,SW_SHOWNA);
    return presented;
}
LRESULT CALLBACK outside_click(int code,WPARAM wp,LPARAM lp) {
    // Reuse one event-driven hook. No mouse polling and no Shell IPC on moves.
    if(code==HC_ACTION&&wp==WM_RBUTTONDOWN){
        auto point=reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;auto window=WindowFromPoint(point);wchar_t type[64]{};
        GetClassNameW(window,type,64);bool desktop=!wcscmp(type,L"SysListView32")||!wcscmp(type,L"SHELLDLL_DefView")||!wcscmp(type,L"WorkerW")||!wcscmp(type,L"Progman");
        auto root=GetAncestor(window,GA_ROOT);GetClassNameW(root,type,64);desktop=desktop&&(!wcscmp(type,L"WorkerW")||!wcscmp(type,L"Progman"));
        if(desktop)arrival_anchor.capture(point,GetTickCount64());else if(wcscmp(type,L"#32768"))arrival_anchor.valid=false;
    }
    if(code==HC_ACTION&&(wp==WM_LBUTTONDOWN||wp==WM_RBUTTONDOWN)&&shelf.expanded){
        auto point=reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;RECT bounds{};GetWindowRect(shelf.window,&bounds);
        if(!PtInRect(&bounds,point)){
            if(shelf.menu){
                wchar_t kind[64]{};auto target=WindowFromPoint(point);GetClassNameW(target,kind,64);
                if(wcscmp(kind,L"#32768")&&!lb::desktop_layer::is_surface(target))shelf.dismiss_after_menu=true;
            }else if(!interaction_busy())PostMessageW(shelf.window,WM_APP+94,0,0);
        }
    }
    return CallNextHookEx(shelf.outside,code,wp,lp);
}
int expanded_height(){RECT work=monitor_work();wchar_t query[260]{};if(shelf.search)GetWindowTextW(shelf.search,query,260);size_t count=show_groups(query)?shelf.model->groups.size()+shelf.views.pinned_count(*shelf.model):shelf.views.list(*shelf.model,g_app.settings.organizer_view,query,shelf.group).size();return metrics().expanded_height(int(count),width(work),std::max(base_height(),int(work.bottom-work.top)-scaled(80)),scaled(58));}
void reflow(){if(!shelf.window)return;KillTimer(shelf.window,1);KillTimer(shelf.window,4);shelf.hovered=shelf.pressed_tile=-1;shelf.hovers.clear();shelf.tiles.clear();shelf.scroll=0;shelf.target=shelf.expanded?expanded_height():base_height();shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.expanded?scaled(48):0);place(shelf.target);InvalidateRect(shelf.window,nullptr,FALSE);}
void invalidate_tile(int index){
    if(index>=0&&size_t(index)<shelf.tiles.size())InvalidateRect(shelf.window,&shelf.tiles[size_t(index)].bounds,FALSE);
}
void hover_to(int index){if(index==shelf.hovered)return;shelf.hovered=index;for(auto& pair:shelf.hovers){pair.second.from=pair.second.value;pair.second.target=0;}if(index>=0){auto& value=shelf.hovers[index];value.from=value.value;value.target=1;}shelf.hover_started=GetTickCount64();if(shelf.reduced_motion){for(auto& pair:shelf.hovers){pair.second.value=pair.second.target;invalidate_tile(pair.first);}}else SetTimer(shelf.window,4,16,nullptr);}
void hover_opacity(bool inside){shelf.mouse_inside=inside;shelf.opacity_motion.aim(std::min(255,g_app.settings.organizer_opacity+(inside?24:0)));shelf.opacity_tick=GetTickCount64();if(shelf.reduced_motion){shelf.opacity_motion.reset(shelf.opacity_motion.target);InvalidateRect(shelf.window,nullptr,FALSE);}else SetTimer(shelf.window,7,16,nullptr);}
void animate(bool expanded) {
    shelf.expanded=expanded;shelf.scroll=0;
    shelf.target=expanded?expanded_height():base_height();hover_to(-1);
    if(!shelf.height_motion.value)shelf.height_motion.reset(shelf.height?shelf.height:base_height());shelf.height_motion.aim(shelf.target);shelf.header_motion.aim(expanded?scaled(48):0);
    shelf.animation=GetTickCount64();if(shelf.reduced_motion){shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.header_motion.target);place(shelf.target);}else SetTimer(shelf.window,1,16,nullptr);
    position_edits();
    ShowWindow(shelf.search,expanded?SW_SHOWNOACTIVATE:SW_HIDE);if(!expanded) { shelf.group.clear();SetWindowTextW(shelf.search,L""); }
    InvalidateRect(shelf.window,nullptr,FALSE);
    if(expanded&&!shelf.fixture&&!shelf.outside) shelf.outside=SetWindowsHookExW(WH_MOUSE_LL,outside_click,g_app.instance,0);
    // Keep the hook while collapsed so a desktop New menu still has an anchor.
}
void schedule_collect() {
    if(!shelf.window||!shelf.model)return;KillTimer(shelf.window,3);if(!g_app.settings.organizer_auto_collect) return;
    auto now=std::chrono::steady_clock::now();std::chrono::milliseconds delay=std::chrono::minutes(1);bool pending=false;
    for(auto const& entry:shelf.model->entries) if(entry.loose&&!entry.manual_loose) { pending=true;delay=std::min(delay,std::chrono::duration_cast<std::chrono::milliseconds>(entry.due-now)); }
    if(pending) SetTimer(shelf.window,3,UINT(std::clamp<int64_t>(delay.count(),interaction_busy()?200:100,60000)),nullptr);
}
std::wstring icon_key(Entry const& entry){return IconCache::shelf_key(entry.path,entry.modified,entry.directory,entry.system);}
HICON icon_for(Entry const& entry,int size) {
    return shelf.icons.get(icon_key(entry),entry.path,entry.directory,entry.system,size);
}
void draw_plate(HDC dc,RECT tile,int radius,COLORREF color,COLORREF edge){
    int opacity=std::clamp(g_app.settings.icon_opacity,0,100);if(!opacity)return;
    if(opacity==100){rounded(dc,tile,radius,color,edge);return;}
    int w=tile.right-tile.left,h=tile.bottom-tile.top;
    auto& cache=shelf.plates;
    auto it=std::find_if(cache.begin(),cache.end(),[&](auto const& p){
        return p.w==w&&p.h==h&&p.radius==radius&&p.color==color&&p.edge==edge;
    });
    if(it==cache.end()){
        if(cache.size()==16)cache.pop_back();
        auto& p=cache.emplace_front();p.w=w;p.h=h;p.radius=radius;p.color=color;p.edge=edge;
        if(!p.surface.reserve(w,h)){cache.pop_front();return;}
        auto dc=p.surface.dc();RECT local{0,0,w,h};unique_gdi<HBRUSH> fill(CreateSolidBrush(color));FillRect(dc,&local,fill.get());
        rounded(dc,local,radius,color,edge);GdiFlush();
        lb::desktop_surface::mask(p.surface.pixels(),p.surface.stride(),w,h,float(radius)/2);
    }else cache.splice(cache.begin(),cache,it);
    auto source=cache.front().surface.dc();
    BLENDFUNCTION blend{AC_SRC_OVER,0,BYTE((opacity*255+50)/100),AC_SRC_ALPHA};
    AlphaBlend(dc,tile.left,tile.top,w,h,source,0,0,w,h,blend);
}
void draw_entry(HDC dc,Entry const& entry,RECT cell,float hover,COLORREF background,bool pressed=false) {
    auto m=metrics();int icon_size=m.icon,zoom=int(std::lround(icon_size*(.17f*hover-(pressed?.06f:0.f))));
    int tile_side=m.tile,cx=(cell.left+cell.right)/2,top=cell.top+scaled(5);RECT tile{cx-tile_side/2,top,cx+(tile_side+1)/2,top+tile_side};
    auto plate=mixed(g_app.settings.icon_color,background,150);if(hover>0||pressed)plate=mixed(plate,foreground(background),255-int(hover*18)-(pressed?12:0));
    draw_plate(dc,tile,scaled(18),plate,mixed(plate,background,180));
    // Cache the resting and enlarged endpoints, never every animation step.
    int size=icon_size+zoom;
    HICON icon=icon_for(entry,zoom>0?int(std::lround(icon_size*1.17)):icon_size);if(icon){DrawIconEx(dc,cx-size/2,top+(tile_side-size)/2,icon,size,size,0,nullptr,DI_NORMAL);}
    if(entry.pin){
        RECT badge{tile.right-scaled(18),tile.top,tile.right+scaled(1),tile.top+scaled(16)};
        rounded(dc,badge,scaled(9),RGB(36,83,71),RGB(144,204,182));
        auto rank=std::to_wstring(entry.pin);
        draw_text(dc,rank.c_str(),badge,shelf.small,RGB(247,255,250),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT label{cell.left+scaled(2),cell.bottom-m.label,cell.right-scaled(2),cell.bottom-scaled(2)};draw_text(dc,entry.name.c_str(),label,shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

}
bool render_frame(HWND window,RECT client,RECT damage,bool opacity_only,POINT* destination) {
    if(!shelf.model||!shelf.surface.reserve(client.right,client.bottom))return false;
    RECT dirty=client;
    if(shelf.painted.cx==client.right&&shelf.painted.cy==client.bottom&&
       shelf.painted_revision==shelf.model->revision&&!shelf.tiles.empty()&&!IsRectEmpty(&damage)){
        if(!IntersectRect(&dirty,&client,&damage))return false;
    }
    bool full=EqualRect(&dirty,&client);
    bool reuse_content=opacity_only&&shelf.foreground_cached&&shelf.painted.cx==client.right&&
        shelf.painted.cy==client.bottom&&shelf.painted_revision==shelf.model->revision&&!shelf.tiles.empty();
    HDC dc=shelf.surface.dc();
    int saved=SaveDC(dc);IntersectClipRect(dc,dirty.left,dirty.top,dirty.right,dirty.bottom);
    COLORREF background=g_app.settings.organizer_color;
    unique_gdi<HBRUSH> brush(CreateSolidBrush(background));FillRect(dc,&dirty,brush.get());
    if(g_app.settings.organizer_glass){COLORREF top=mixed(background,RGB(226,242,247),230),bottom=mixed(background,RGB(18,27,37),235);TRIVERTEX vertices[]={{0,0,COLOR16(GetRValue(top)<<8),COLOR16(GetGValue(top)<<8),COLOR16(GetBValue(top)<<8),0},{client.right,client.bottom,COLOR16(GetRValue(bottom)<<8),COLOR16(GetGValue(bottom)<<8),COLOR16(GetBValue(bottom)<<8),0}};GRADIENT_RECT mesh{0,1};GradientFill(dc,vertices,2,&mesh,1,GRADIENT_FILL_RECT_V);}
    {unique_gdi<HPEN> edge(CreatePen(PS_SOLID,1,mixed(background,RGB(225,242,250),220)));auto pen=SelectObject(dc,edge.get()),fill=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,1,1,client.right-1,client.bottom-1,scaled(36),scaled(36));SelectObject(dc,pen);SelectObject(dc,fill);}
    int cell=cell_size(),row=row_size(),columns=metrics().columns(client.right);wchar_t query[260]{};GetWindowTextW(shelf.search,query,260);
    bool groups=show_groups(query);
    if(full&&!reuse_content){
        auto const* entries=groups?nullptr:&shelf.views.list(*shelf.model,g_app.settings.organizer_view,query,shelf.group);
        size_t pins=groups&&!shelf.drag_groups?shelf.views.pinned_count(*shelf.model):0;
        size_t count=groups?pins+shelf.model->groups.size():entries->size();
        int top=scaled(10)+int(std::lround(shelf.header_motion.value));
        int limit=shelf.expanded?columns*std::max(1,int(client.bottom-top-scaled(6))/row):std::min(10,columns-1);
        size_t first=shelf.expanded?size_t(std::max(0,shelf.scroll))*size_t(columns):0;
        shelf.tiles.clear();
        for(size_t n=first;n<count&&n<first+size_t(limit);++n){
            size_t at=n-first;
            int x=scaled(12)+int(at%size_t(columns))*cell,y=top+int(at/size_t(columns))*row;
            bool group_tile=groups&&n>=pins;
            size_t index=group_tile?n-pins:groups?shelf.views.list(*shelf.model,0,L"",L"")[n]:(*entries)[n];
            auto const& identity=group_tile?shelf.model->groups[index].id:shelf.model->entries[index].path;
            shelf.tiles.push_back({{x,y,x+cell,y+row},index,group_tile,identity});
        }
        if(!shelf.expanded)shelf.tiles.push_back({{client.right-cell-scaled(12),scaled(10),client.right-scaled(12),scaled(10)+row},SIZE_MAX,false});
    }
    auto content=[&](HDC dc){
    for(size_t i=0;i<shelf.tiles.size();++i){
        auto const& item=shelf.tiles[i];RECT bounds=item.bounds,intersection{};
        if(item.index==SIZE_MAX||!IntersectRect(&intersection,&dirty,&bounds))continue;
        auto hover_it=shelf.hovers.find(int(i));float hover=hover_it==shelf.hovers.end()?0.f:hover_it->second.value;
        bool pressed=shelf.pressed_tile==int(i)&&shelf.hovered==shelf.pressed_tile;
        if(item.group) {
            auto const& group=shelf.model->groups[item.index];int side=metrics().tile,cx=(bounds.left+bounds.right)/2,top_tile=bounds.top+scaled(5);RECT tile{cx-side/2,top_tile,cx+(side+1)/2,top_tile+side};draw_plate(dc,tile,scaled(14),mixed(group.color,background,120),mixed(group.color,background,85));
            auto const& covers=shelf.views.group_covers(*shelf.model);auto cover=covers.find(group.id);int gap=scaled(4),mini=(side-scaled(14)-gap)/2,grid=2*mini+gap,origin_x=cx-grid/2,origin_y=top_tile+(side-grid)/2;
            for(size_t k=0;cover!=covers.end()&&k<cover->second.size();++k)if(auto icon=icon_for(shelf.model->entries[cover->second[k]],hover>0?mini+int(mini*.1f):mini)){int zoom=int(mini*(.1f*hover-(pressed?.06f:0.f)));DrawIconEx(dc,origin_x+int(k%2)*(mini+gap)-zoom/2,origin_y+int(k/2)*(mini+gap)-zoom/2,icon,mini+zoom,mini+zoom,0,nullptr,DI_NORMAL);}
            draw_text(dc,group.name.c_str(),RECT{bounds.left+scaled(2),bounds.bottom-metrics().label,bounds.right-scaled(2),bounds.bottom-scaled(2)},shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        } else { draw_entry(dc,shelf.model->entries[item.index],bounds,hover,background,pressed); }
    }
    if(!shelf.expanded&&shelf.tiles.back().bounds.left<dirty.right) { RECT search=shelf.tiles.back().bounds;int cx=(search.left+search.right)/2,cy=search.top+scaled(5)+metrics().tile/2,r=scaled(10);unique_gdi<HPEN> pen(CreatePen(PS_SOLID,scaled(2),foreground(background)));auto prior=SelectObject(dc,pen.get()),prior_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,cx-r,cy-r,cx+r,cy+r);MoveToEx(dc,cx+scaled(7),cy+scaled(7),nullptr);LineTo(dc,cx+scaled(15),cy+scaled(15));SelectObject(dc,prior_brush);SelectObject(dc,prior);draw_text(dc,tr(L"搜索",L"Search"),RECT{search.left,search.bottom-metrics().label,search.right,search.bottom-scaled(2)},shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE); }
    if(shelf.expanded&&!shelf.group.empty()) { auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& group){return group.id==shelf.group;});if(found!=shelf.model->groups.end()) draw_text(dc,found->name.c_str(),RECT{scaled(24),scaled(12),client.right-scaled(360),scaled(48)},shelf.heading,mixed(foreground(background),background,int(std::clamp(shelf.header_motion.value/std::max(1,scaled(48)),0.,1.)*255)),DT_SINGLELINE|DT_VCENTER); }
    };
    BYTE opacity=BYTE(std::clamp(shelf.opacity_motion.value,0.,255.));
    bool separate=opacity<255&&shelf.content_black.reserve(client.right,client.bottom)&&shelf.content_white.reserve(client.right,client.bottom);
    if(separate){
        if(!reuse_content){
            int black=SaveDC(shelf.content_black.dc()),white=SaveDC(shelf.content_white.dc());
            for(auto pass:{shelf.content_black.dc(),shelf.content_white.dc()})IntersectClipRect(pass,dirty.left,dirty.top,dirty.right,dirty.bottom);
            FillRect(shelf.content_black.dc(),&dirty,HBRUSH(GetStockObject(BLACK_BRUSH)));
            FillRect(shelf.content_white.dc(),&dirty,HBRUSH(GetStockObject(WHITE_BRUSH)));
            content(shelf.content_black.dc());content(shelf.content_white.dc());
            RestoreDC(shelf.content_black.dc(),black);RestoreDC(shelf.content_white.dc(),white);
        }
        GdiFlush();
        for(int y=dirty.top;y<dirty.bottom;++y)for(int x=dirty.left;x<dirty.right;++x){
            auto& pixel=shelf.surface.pixels()[size_t(y)*shelf.surface.stride()+x];
            pixel=lb::desktop_surface::composite(pixel,shelf.content_black.pixels()[size_t(y)*shelf.content_black.stride()+x],shelf.content_white.pixels()[size_t(y)*shelf.content_white.stride()+x],opacity);
        }
    }else content(dc);
    shelf.foreground_cached=separate&&(full||shelf.foreground_cached);
    RestoreDC(dc,saved);
    if(shelf.surface.present(window,client.right,client.bottom,255,float(scaled(18)),!separate,full?nullptr:&dirty,destination)){
        shelf.painted={client.right,client.bottom};
        shelf.painted_revision=shelf.model->revision;
        return true;
    }
    shelf.painted={};return false;
}
void paint(HWND window){
    PAINTSTRUCT ps{};BeginPaint(window,&ps);
    RECT client{};GetClientRect(window,&client);
    render_frame(window,client,ps.rcPaint);
    EndPaint(window,&ps);
}
int hit(POINT point) { for(size_t n=0;n<shelf.tiles.size();++n) if(PtInRect(&shelf.tiles[n].bounds,point)&&shelf.model&&shelf.tiles[n].current(*shelf.model)) return int(n);return -1; }
bool model_result(bool ok) { if(!ok)set_status(L"收纳更改未能保存，已恢复原状态；原文件未改动。",L"Organizer changes could not be saved and were reverted. Files are untouched.");return ok; }
void launch(Entry const& entry) { ShellExecuteW(shelf.window,L"open",entry.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);animate(false); }
void rebuild_loose();
void apply_listing(std::vector<RootListing> const& listing) {
    if(!shelf.model)return;
    auto revision=shelf.model->revision;
    std::set<std::wstring> before;if(shelf.model&&arrival_anchor.recent(GetTickCount64()))for(auto const& entry:shelf.model->entries)before.insert(entry.path);
    if(shelf.model->apply_scan(listing,true,GetTickCount64()<shelf.protect_empty_until)) {
        if(revision==shelf.model->revision)return;
        if(arrival_anchor.recent(GetTickCount64())) {
            auto entries=shelf.model->entries;unsigned placed=0;
            for(auto& entry:shelf.model->entries)if(entry.loose&&!entry.positioned&&!before.contains(entry.path)) {
                int offset=int(placed++)*scaled(24);entry.positioned=true;
                entry.position={arrival_anchor.point.x-std::max(scaled(184),cell_size())/2+offset,arrival_anchor.point.y-scaled(5)+offset};
            }
            if(placed){arrival_anchor.valid=false;if(!shelf.model->save())shelf.model->entries=std::move(entries);}
        }
        rebuild_loose();schedule_collect();reflow();
    }else if(shelf.window) SetTimer(shelf.window,2,5000,nullptr);
}
std::shared_ptr<DirectoryScan> begin_scan(std::vector<std::wstring> const& roots){
    try{
        auto job=std::make_shared<DirectoryScan>();
        std::thread([job,roots]{
            try{job->listing=scan_roots(roots,&job->cancelled);}catch(...){job->listing.clear();}
            job->done=true;
        }).detach();
        return job;
    }catch(...){return {};}
}
void synchronize(){
    if(!shelf.window||!shelf.model||shelf.scan)return;
    if(interaction_busy()){SetTimer(shelf.window,2,200,nullptr);return;}
    shelf.scan=begin_scan(shelf.roots);
    if(!shelf.scan){SetTimer(shelf.window,2,5000,nullptr);return;}
    shelf.scan->serial=shelf.change_serial;shelf.scan->revision=shelf.model->revision;
    SetTimer(shelf.window,9,50,nullptr);
}
void finish_scan(){
    if(!shelf.scan||!shelf.scan->done)return;
    KillTimer(shelf.window,9);
    auto job=std::move(shelf.scan);
    if(job->serial!=shelf.change_serial||job->revision!=shelf.model->revision||interaction_busy()){
        SetTimer(shelf.window,2,100,nullptr);
        return;
    }
    apply_listing(job->listing);
}
bool finish_file_rename(){
    if(!shelf.rename)return true;if(!shelf.rename->done)return false;
    auto job=std::move(shelf.rename);if(job->worker.joinable())job->worker.join();
    if(job->ok&&shelf.model){for(auto& e:shelf.model->entries)if(e.path==job->source){e.path=job->target;if(!e.custom_name){auto path=std::filesystem::path(e.path);e.name=!e.directory&&!_wcsicmp(path.extension().c_str(),L".lnk")?path.stem().wstring():path.filename().wstring();}break;}model_result(shelf.model->save());}
    else if(!job->ok&&!shelf.fixture&&!g_app.close_requested)error_box(shelf.window,tr(L"无法重命名：请检查文件是否被占用、名称是否合法或目标是否已存在。原文件已保留。",L"Cannot rename: check the name, file permissions and whether the target exists. Existing files were preserved."),E_FAIL);
    if(shelf.window){KillTimer(shelf.window,8);rebuild_loose();schedule_collect();reflow();desktop_changed();}return true;
}
std::wstring shortcut_extension(std::filesystem::path const& path,bool directory){
    if(directory) return {};
    auto ext=path.extension().wstring();
    for(auto known:{L".lnk",L".url",L".website",L".appref-ms"})
        if(!_wcsicmp(ext.c_str(),known)) return ext;
    return {};
}
bool start_file_rename(std::wstring const& path,std::wstring name){
    if(shelf.rename||!shelf.model||!safe_filename(name.c_str()))return false;
    auto item=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& entry){return entry.path==path;});
    if(item==shelf.model->entries.end()||item->system)return false;
    std::filesystem::path source(path);
    auto ext=shortcut_extension(source,item->directory);
    if(!ext.empty()&&_wcsicmp(std::filesystem::path(name).extension().c_str(),ext.c_str())) name+=ext;
    if(name.size()>255||!safe_filename(name.c_str()))return false;
    auto target=(source.parent_path()/name).wstring();if(path==target)return true;
    auto job=std::make_unique<RenameJob>();job->source=path;job->target=target;auto raw=job.get();auto data=std::filesystem::path(g_app.data);
    try{job->worker=std::thread([raw,data]{try{com_apartment apartment(COINIT_MULTITHREADED);auto plan=lb::files::preview({raw->source},L"{name}{ext}");plan.front().target=raw->target;lb::files::Journal journal(data/L"file-journal.db");journal.apply(plan);raw->ok=true;}catch(...){raw->ok=false;}raw->done=true;});}catch(...){return false;}
    shelf.rename=std::move(job);if(shelf.window)SetTimer(shelf.window,8,50,nullptr);return true;
}
void rename_group(size_t index) {
    if(index>=shelf.model->groups.size()) return;auto &group=shelf.model->groups[index];shelf.group=group.id;
    if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.editing_entry.clear();shelf.editing_file=false;shelf.editor_anchor=shelf.window;shelf.editor_bounds={scaled(22),scaled(10),scaled(282),scaled(42)};shelf.editor=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CLIENTEDGE,L"EDIT",group.name.c_str(),WS_POPUP|ES_AUTOHSCROLL,0,0,scaled(260),scaled(32),shelf.window,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.editor,GWLP_ID,2);lb::window_shape::own_frame(shelf.editor);SendMessageW(shelf.editor,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.editor,EM_SETLIMITTEXT,120,0);SetWindowSubclass(shelf.editor,edit_keys,1,0);position_edits();ShowWindow(shelf.editor,SW_SHOWNOACTIVATE);SetForegroundWindow(shelf.editor);SetFocus(shelf.editor);SendMessageW(shelf.editor,EM_SETSEL,0,-1);
}
void rename_entry(std::wstring const& path,RECT bounds,bool file=false,HWND anchor=nullptr) {
    auto found=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==path;});if(found==shelf.model->entries.end())return;
    if(shelf.rename)return;if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.editing_entry=path;shelf.editing_file=file&&!found->system;shelf.editor_anchor=anchor?anchor:shelf.window;auto text=shelf.editing_file?std::filesystem::path(path).filename().wstring():found->name;
    auto ext=shelf.editing_file?shortcut_extension(path,found->directory):std::wstring{};
    auto selection_end=ext.empty()?LPARAM(-1):LPARAM(text.size()-ext.size());
    RECT client{};GetClientRect(shelf.editor_anchor,&client);int w=std::min(scaled(260),int(client.right)-scaled(24)),x=std::clamp(int(bounds.left),scaled(12),int(client.right)-w-scaled(12)),y=std::max(scaled(10),int(bounds.bottom)-metrics().label);
    shelf.editor_bounds={x,y,x+w,y+scaled(30)};shelf.editor=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CLIENTEDGE,L"EDIT",text.c_str(),WS_POPUP|ES_AUTOHSCROLL,0,0,w,scaled(30),shelf.editor_anchor,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.editor,GWLP_ID,2);lb::window_shape::own_frame(shelf.editor);SendMessageW(shelf.editor,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.editor,EM_SETLIMITTEXT,shelf.editing_file?255:240,0);SetWindowSubclass(shelf.editor,edit_keys,1,0);position_edits();ShowWindow(shelf.editor,SW_SHOWNOACTIVATE);SetForegroundWindow(shelf.editor);SetFocus(shelf.editor);SendMessageW(shelf.editor,EM_SETSEL,0,selection_end);
}
void commit_title(bool confirm_file=false) {
    if(!shelf.editor)return;auto edit=std::exchange(shelf.editor,nullptr);wchar_t title[258]{};GetWindowTextW(edit,title,258);auto path=std::exchange(shelf.editing_entry,{});bool file=std::exchange(shelf.editing_file,false);shelf.editor_anchor=nullptr;log_line(L"organizer title commit file=%d length=%u",file,unsigned(wcslen(title)));
    if(file){
        DestroyWindow(edit);
        if(confirm_file&&!start_file_rename(path,title)&&!shelf.fixture)
            error_box(shelf.window,tr(L"文件名无效或项目已更改，原文件未改动。",L"Invalid name or the item changed. The original file was retained."),E_INVALIDARG);
        schedule_collect();return;
    }
    if(shelf.model){if(!path.empty())model_result(shelf.model->rename_entry(path,title));else {auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& group){return group.id==shelf.group;});if(found!=shelf.model->groups.end()){auto before=*found;found->name=title;found->automatic=false;if(!model_result(shelf.model->save()))*found=std::move(before);}}}
    DestroyWindow(edit);if(shelf.window){InvalidateRect(shelf.window,nullptr,FALSE);schedule_collect();}
}
void context_menu(POINT point,int tile) {
    if(shelf.rename)return;MenuGuard guard;HMENU menu=CreatePopupMenu();scope_exit destroy([&]{DestroyMenu(menu);});std::wstring group_id,path;RECT bounds{};bool system=false,external=false;std::vector<std::wstring> targets;
    // The popup pumps messages. Retain identifiers, never tile indices or Entry
    // references, across that nested loop (refresh/disable can replace either).
    if(tile>=0&&size_t(tile)<shelf.tiles.size()&&shelf.tiles[size_t(tile)].current(*shelf.model)){auto item=shelf.tiles[size_t(tile)];bounds=item.bounds;if(item.group&&item.index<shelf.model->groups.size())group_id=shelf.model->groups[item.index].id;else if(item.index<shelf.model->entries.size()){auto const& entry=shelf.model->entries[item.index];path=entry.path;group_id=entry.group;system=entry.system;external=entry.external;}}
    lb::shell_menu::Menu native;
    AppendMenuW(menu,MF_STRING|(shelf.model->can_undo()?0:MF_GRAYED),26,tr(L"撤销上一次收纳",L"Undo last organizer action"));
    if(!path.empty()){
        auto& entries=shelf.model->entries;
        auto found=std::find_if(entries.begin(),entries.end(),[&](auto const& e){return e.path==path;});
        bool pinned=found!=entries.end()&&found->pin;
        bool full=std::count_if(entries.begin(),entries.end(),[](auto const& e){return e.pin!=0;})>=10;
        AppendMenuW(menu,MF_STRING|(!pinned&&full?MF_GRAYED:0),pinned?25:24,pinned?tr(L"取消首排固定",L"Unpin from front row"):tr(L"固定到首排",L"Pin to front row"));
        AppendMenuW(menu,MF_STRING,20,tr(L"打开",L"Open"));if(!system)AppendMenuW(menu,MF_STRING,21,tr(L"在文件夹中显示",L"Show in folder"));AppendMenuW(menu,MF_STRING,22,tr(L"更改显示名称…",L"Change display name…"));
        HMENU move=CreatePopupMenu();for(auto const& group:shelf.model->groups)if(group.id!=group_id){targets.push_back(group.id);AppendMenuW(move,MF_STRING,100+targets.size()-1,group.name.empty()?tr(L"未命名分组",L"Untitled group"):group.name.c_str());}if(!targets.empty())AppendMenuW(menu,MF_POPUP,UINT_PTR(move),tr(L"移至分组",L"Move to group"));else DestroyMenu(move);
        AppendMenuW(menu,MF_STRING,23,external&&!system?tr(L"移除快捷入口（保留原文件）",L"Remove shortcut (keep file)"):tr(L"还原到原桌面",L"Restore to desktop"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    }else if(!group_id.empty()) { AppendMenuW(menu,MF_STRING,1,tr(L"重命名分组",L"Rename group"));AppendMenuW(menu,MF_STRING,2,tr(L"分组颜色…",L"Group color…"));AppendMenuW(menu,MF_STRING|(group_id==L"other"?MF_GRAYED:0),3,tr(L"删除分组（保留文件）",L"Remove group (keep files)"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr); }
    if(path.empty()){AppendMenuW(menu,MF_STRING,10,tr(L"添加新分组",L"New group"));AppendMenuW(menu,MF_STRING,11,tr(L"按名称排列",L"Sort by name"));AppendMenuW(menu,MF_STRING,12,tr(L"按修改时间排列",L"Sort by modified time"));AppendMenuW(menu,MF_STRING,13,tr(L"按分组排列",L"Group view"));AppendMenuW(menu,MF_STRING,14,tr(L"移动位置",L"Move shelf"));AppendMenuW(menu,MF_STRING,15,tr(L"重新同步桌面",L"Refresh desktop"));AppendMenuW(menu,MF_STRING,16,tr(L"新建便签",L"New note"));AppendMenuW(menu,MF_STRING,17,tr(L"收纳设置…",L"Organizer settings…"));AppendMenuW(menu,MF_STRING,18,tr(L"关闭并恢复原生桌面",L"Disable and restore desktop"));}
    if(!path.empty())native.add_submenu(shelf.window,menu,path,GetKeyState(VK_SHIFT)<0);
    if(!shelf.fixture)SetForegroundWindow(shelf.window);int id=popup_menu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,shelf.window,nullptr);
    if(!guard.alive())return;
    if(native.owns(UINT(id))){log_line(L"organizer shell rename=%d",native.is_verb(UINT(id),"rename"));if(native.is_verb(UINT(id),"rename"))rename_entry(path,bounds,true);else {native.invoke(UINT(id),point);if(guard.alive())desktop_changed();}return;}
    auto group_index=[&]{auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[&](auto const& g){return g.id==group_id;});return size_t(found-shelf.model->groups.begin());};
    if(!path.empty()&&id>=100&&size_t(id-100)<targets.size()){if(model_result(shelf.model->collect(path,targets[size_t(id-100)])))PostMessageW(shelf.window,WM_APP+95,1,0);}
    else if(!path.empty()&&id==20){auto found=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==path;});if(found!=shelf.model->entries.end())launch(*found);}
    else if(!path.empty()&&id==21&&!system){PIDLIST_ABSOLUTE item=nullptr;if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr))){SHOpenFolderAndSelectItems(item,0,nullptr,0);CoTaskMemFree(item);}}
    else if((id==24||id==25)&&!path.empty()){model_result(shelf.model->set_pin(path,id==24?10:0));reflow();}
    else if(id==26){if(model_result(shelf.model->undo()))PostMessageW(shelf.window,WM_APP+95,0,0);}
    else if(!path.empty()&&id==22)rename_entry(path,bounds);
    else if(!path.empty()&&id==23){if(model_result(shelf.model->restore_entry(path)))PostMessageW(shelf.window,WM_APP+95,1,0);}
    else if(id>=1&&id<=3&&group_index()<shelf.model->groups.size()) {
        if(id==1){shelf.group=group_id;animate(true);rename_group(group_index());}
        else if(id==2){static COLORREF colors[16];CHOOSECOLORW choose{sizeof choose};choose.hwndOwner=shelf.window;choose.rgbResult=shelf.model->groups[group_index()].color;choose.lpCustColors=colors;choose.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&choose)&&guard.alive()&&group_index()<shelf.model->groups.size()){auto& group=shelf.model->groups[group_index()];auto prior=group.color;group.color=choose.rgbResult;if(!model_result(shelf.model->save()))group.color=prior;}}
        else {if(model_result(shelf.model->remove_group(group_id)))animate(false);}
    }
    else if(id==10) {auto before=*shelf.model;shelf.group=shelf.model->create_group(tr(L"新分组",L"New group")).id;if(model_result(shelf.model->save())){animate(true);rename_group(shelf.model->groups.size()-1);}else {*shelf.model=std::move(before);shelf.group.clear();}}
    else if(id>=11&&id<=13) { g_app.settings.organizer_view=id-11;g_app.settings.organizer_views[std::clamp(g_app.settings.organizer_alignment,0,2)]=id-11;save_config();animate(false); }
    else if(id==14) { shelf.moving=true;set_status(L"可拖动收纳背景移动位置，按 Esc 结束",L"Drag the shelf background to move it. Escape finishes."); }
    else if(id==15) desktop_changed();else if(id==16) note_create(shelf.window);else if(id==17) settings_show(1);
    else if(id==18) { g_app.settings.organizer_enabled=0;save_config();shutdown(); }
    if(guard.alive())InvalidateRect(shelf.window,nullptr,FALSE);
}
void appearance(HWND window,int radius) {
    (void)radius;
    lb::window_shape::own_frame(window);
    struct Accent { int state,flags;DWORD color;int animation; };struct Data { int attribute;void *data;size_t size; };
    using Composition=BOOL(WINAPI*)(HWND,Data*);auto composition=reinterpret_cast<Composition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
    if(composition) { Accent accent{};Data data{19,&accent,sizeof accent};composition(window,&data); }
    int backdrop=1;DwmSetWindowAttribute(window,38,&backdrop,sizeof backdrop);
    DWM_BLURBEHIND blur{};blur.dwFlags=DWM_BB_ENABLE;DwmEnableBlurBehindWindow(window,&blur);
    SetWindowRgn(window,nullptr,FALSE);
    BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);shelf.reduced_motion=!animation;
    shelf.opacity_motion.reset(std::min(255,g_app.settings.organizer_opacity+(shelf.mouse_inside?24:0)));
    if(shelf.edit_brush)DeleteObject(shelf.edit_brush);shelf.edit_brush=CreateSolidBrush(g_app.settings.organizer_color);
    InvalidateRect(window,nullptr,FALSE);
}
// Native EDIT controls remain editable/IME-accessible owned popups, anchored
// inside the alpha surface. They never create independent taskbar buttons.
void position_edits(){
    if(!shelf.window)return;RECT bounds{};GetWindowRect(shelf.window,&bounds);int width=bounds.right-bounds.left;
    int edit_width=std::min(scaled(360),std::max(scaled(100),width-scaled(48)));
    if(shelf.search){
        RECT target{bounds.right-scaled(24)-edit_width,bounds.top+scaled(13),bounds.right-scaled(24),bounds.top+scaled(13)+scaled(32)},current{};
        GetWindowRect(shelf.search,&current);
        if(!EqualRect(&target,&current))SetWindowPos(shelf.search,nullptr,target.left,target.top,edit_width,scaled(32),SWP_NOACTIVATE|SWP_NOZORDER);
        int alpha=int(std::clamp(shelf.header_motion.value/std::max(1,scaled(48))*255,1.,255.));
        if(alpha!=shelf.search_alpha&&SetLayeredWindowAttributes(shelf.search,0,BYTE(alpha),LWA_ALPHA))shelf.search_alpha=alpha;
    }
    if(shelf.editor){if(IsWindow(shelf.editor_anchor))GetWindowRect(shelf.editor_anchor,&bounds);auto r=shelf.editor_bounds;SetWindowPos(shelf.editor,nullptr,bounds.left+r.left,bounds.top+r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_NOZORDER);}
}
LRESULT CALLBACK edit_keys(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    if(message==WM_NCPAINT){HDC dc=GetWindowDC(window);RECT rect{};GetWindowRect(window,&rect);OffsetRect(&rect,-rect.left,-rect.top);unique_gdi<HBRUSH> edge(CreateSolidBrush(mixed(g_app.settings.organizer_color,foreground(g_app.settings.organizer_color),190)));FrameRect(dc,&rect,edge.get());InflateRect(&rect,-1,-1);unique_gdi<HBRUSH> inside(CreateSolidBrush(g_app.settings.organizer_color));FrameRect(dc,&rect,inside.get());ReleaseDC(window,dc);return 0;}
    if(message==WM_MOUSEMOVE&&window==shelf.search){
        if(!shelf.mouse_inside)hover_opacity(true);
        TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,window,0};TrackMouseEvent(&track);
    }
    if(message==WM_MOUSELEAVE&&window==shelf.search&&shelf.window)PostMessageW(shelf.window,WM_MOUSELEAVE,0,0);
    (void)data;if(message==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)) { if(id==1) { if(window!=shelf.editor)return 0;if(wp==VK_RETURN) commit_title(true);else { auto edit=std::exchange(shelf.editor,nullptr);shelf.editing_entry.clear();shelf.editing_file=false;shelf.editor_anchor=nullptr;DestroyWindow(edit);schedule_collect(); } }else if(wp==VK_ESCAPE) animate(false);return 0; }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,edit_keys,id);
    if(message==WM_KILLFOCUS&&id==1&&shelf.editor==window&&shelf.window)PostMessageW(shelf.window,WM_COMMAND,MAKEWPARAM(2,EN_KILLFOCUS),LPARAM(window));
    auto result=DefSubclassProc(window,message,wp,lp);
    if(id==2&&shelf.window&&(message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==WM_IME_COMPOSITION))PostMessageW(shelf.window,WM_COMMAND,MAKEWPARAM(1,EN_CHANGE),LPARAM(window));
    return result;
}
void rebuild_loose() {
    if(interaction_busy()){if(shelf.window)SetTimer(shelf.window,5,200,nullptr);return;}
    std::unordered_map<std::wstring,Entry const*> pending;
    for(auto const& entry:shelf.model->entries)if(entry.loose)pending.emplace(entry.path,&entry);
    RECT work=monitor_work();
    int loose_width=std::max(scaled(184),cell_size()),loose_height=row_size()+scaled(30);
    std::erase_if(shelf.loose,[&](auto const& item){
        auto found=pending.find(item->path);
        if(found==pending.end()){
            if(item->window)DestroyWindow(item->window);
            return true;
        }
        if(item->window){
            RECT size{};GetClientRect(item->window,&size);
            if(size.right!=loose_width||size.bottom!=loose_height)
                SetWindowPos(item->window,nullptr,0,0,loose_width,loose_height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            if(!found->second->manual_loose&&g_app.settings.organizer_auto_collect)item->hint=true;
            InvalidateRect(item->window,nullptr,FALSE);
        }
        pending.erase(found);return false;
    });
    size_t count=shelf.loose.size();
    for(auto const& entry:shelf.model->entries)if(entry.loose&&pending.contains(entry.path)){
        auto item=std::make_unique<Loose>();item->path=entry.path;int rows=std::max(1,int(work.bottom-work.top)/(loose_height+scaled(8)));item->position={work.left+scaled(18)+int(count/size_t(rows))*(loose_width+scaled(8)),work.top+scaled(12)+int(count%size_t(rows))*(loose_height+scaled(8))};if(entry.positioned){MONITORINFO info{sizeof info};if(GetMonitorInfoW(MonitorFromPoint(entry.position,MONITOR_DEFAULTTONEAREST),&info))item->position={std::clamp(entry.position.x,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-loose_width)),std::clamp(entry.position.y,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-loose_height))};}++count;
        item->window=CreateWindowExW((floating_preview()?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_LAYERED,L"LightBlockDesktopNative.LooseIcon",entry.name.c_str(),WS_POPUP,item->position.x,item->position.y,loose_width,loose_height,floating_preview()?nullptr:GetShellWindow(),nullptr,g_app.instance,item.get());if(item->window) { lb::desktop_layer::mark(item->window);SetLayeredWindowAttributes(item->window,0,255,LWA_ALPHA);lb::window_shape::own_frame(item->window);lb::window_shape::clip(item->window,scaled(18));if(floating_preview()&&!shelf.fixture)SetWindowPos(item->window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);else lb::desktop_layer::place(item->window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|(shelf.fixture?0:SWP_SHOWWINDOW));SetTimer(item->window,1,g_app.settings.organizer_auto_collect&&!entry.manual_loose?1000:10000,nullptr);shelf.loose.push_back(std::move(item)); }
    }
}
RECT hint_bounds(HWND window){RECT bounds{};GetClientRect(window,&bounds);return {scaled(5),row_size(),bounds.right-scaled(5),bounds.bottom-scaled(3)};}
bool hit_hint(HWND window,Loose const& item,POINT point){auto bounds=hint_bounds(window);return item.hint&&PtInRect(&bounds,point);}
LRESULT CALLBACK loose_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto item=reinterpret_cast<Loose*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE) { item=static_cast<Loose*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(item)); }
    if(!item) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_WINDOWPOSCHANGING:if(!shelf.fixture&&!floating_preview())lb::desktop_layer::protect_position(*reinterpret_cast<WINDOWPOS*>(lp),lb::desktop_layer::host());break;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:lb::window_shape::clip(window,scaled(18));return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT bounds{};GetClientRect(window,&bounds);unique_gdi<HBRUSH> brush(CreateSolidBrush(g_app.settings.organizer_color));FillRect(dc,&bounds,brush.get());
        auto entry=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==item->path;});
        if(entry!=shelf.model->entries.end()){item->icon_key=icon_key(*entry);RECT cell{0,0,bounds.right,row_size()};draw_entry(dc,*entry,cell,false,g_app.settings.organizer_color);}
        if(item->hint){
            bool automatic=entry!=shelf.model->entries.end()&&!entry->manual_loose&&g_app.settings.organizer_auto_collect;auto bubble=hint_bounds(window);
            rounded(dc,bubble,scaled(12),mixed(g_app.settings.organizer_color,RGB(166,228,202),item->hint_pressed?150:item->hint_hover?180:210),g_app.settings.organizer_color);
            auto label=item->hint_hover?tr(L"点击直接收纳",L"Click to collect now"):automatic?tr(L"1 分钟后自动收纳",L"Auto-collect after 1 min"):tr(L"右键放入收纳",L"Right-click to collect");
            draw_text(dc,label,bubble,shelf.small,foreground(g_app.settings.organizer_color),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }EndPaint(window,&ps);return 0;
    }
    case WM_TIMER:{KillTimer(window,1);auto entry=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==item->path;});if(entry!=shelf.model->entries.end()&&!entry->manual_loose&&g_app.settings.organizer_auto_collect){item->hint=true;schedule_collect();}else{item->hint=false;}InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_LBUTTONDBLCLK:if(!hit_hint(window,*item,POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}))ShellExecuteW(window,L"open",item->path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;
    case WM_LBUTTONDOWN:if(hit_hint(window,*item,POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})){item->hint_pressed=true;SetCapture(window);InvalidateRect(window,nullptr,FALSE);}else{GetCursorPos(&item->pressed);item->dragging=true;SetCapture(window);}return 0;
    case WM_MOUSEMOVE:{bool hover=hit_hint(window,*item,POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});if(hover!=item->hint_hover){item->hint_hover=hover;auto bubble=hint_bounds(window);InvalidateRect(window,&bubble,FALSE);}TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,window,0};TrackMouseEvent(&track);if(item->dragging) { POINT point;GetCursorPos(&point);RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,rect.left+point.x-item->pressed.x,rect.top+point.y-item->pressed.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);item->pressed=point; }return 0;}
    case WM_MOUSELEAVE:item->hint_hover=false;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_SETCURSOR:if(item->hint_hover){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
    case WM_LBUTTONUP:{auto path=item->path;bool hint=item->hint_pressed,drag=item->dragging;item->hint_pressed=item->dragging=false;RECT rect{};GetWindowRect(window,&rect);ReleaseCapture();if(hint){if(hit_hint(window,*item,POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)})&&model_result(shelf.model->collect(path,L"")))PostMessageW(shelf.window,WM_APP+95,0,0);InvalidateRect(window,nullptr,FALSE);schedule_collect();return 0;}if(!drag)return 0;POINT point{};GetCursorPos(&point);ScreenToClient(shelf.window,&point);int target=hit(point);if(target>=0&&shelf.tiles[size_t(target)].group){model_result(shelf.model->collect(path,shelf.model->groups[shelf.tiles[size_t(target)].index].id));PostMessageW(shelf.window,WM_APP+95,0,0);}else model_result(shelf.model->position_entry(path,POINT{rect.left,rect.top}));schedule_collect();return 0;}
    case WM_CAPTURECHANGED:item->dragging=item->hint_pressed=false;InvalidateRect(window,nullptr,FALSE);schedule_collect();return 0;
    case WM_CONTEXTMENU: { if(shelf.rename)return 0;MenuGuard guard;auto path=item->path;std::vector<std::wstring> targets;HMENU menu=CreatePopupMenu();scope_exit destroy([&]{DestroyMenu(menu);});lb::shell_menu::Menu native;AppendMenuW(menu,MF_STRING,1,tr(L"放入收纳",L"Collect"));for(auto const& group:shelf.model->groups) { targets.push_back(group.id);auto label=std::wstring(tr(L"放入分组：",L"Move to: "))+(group.name.empty()?tr(L"未命名分组",L"Untitled group"):group.name.c_str());AppendMenuW(menu,MF_STRING,99+targets.size(),label.c_str()); }native.add_submenu(window,menu,path,GetKeyState(VK_SHIFT)<0);POINT point;GetCursorPos(&point);if(!shelf.fixture)SetForegroundWindow(window);int selected=popup_menu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);if(!guard.alive()||!IsWindow(window)||GetWindowLongPtrW(window,GWLP_USERDATA)!=LPARAM(item))return 0;if(native.owns(UINT(selected))){log_line(L"organizer shell rename=%d",native.is_verb(UINT(selected),"rename"));if(native.is_verb(UINT(selected),"rename")){RECT bounds{};GetClientRect(window,&bounds);bounds.bottom=row_size();rename_entry(path,bounds,true,window);}else {native.invoke(UINT(selected),point);if(guard.alive())desktop_changed();}return 0;}if(selected==1||(selected>=100&&size_t(selected-100)<targets.size())) { auto group=selected>=100?targets[size_t(selected-100)]:L"";if(model_result(shelf.model->collect(path,group)))PostMessageW(shelf.window,WM_APP+95,1,0); }return 0; }
    case WM_DESTROY:RemovePropW(window,lb::desktop_layer::surface_property);KillTimer(window,1);return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);return DefWindowProcW(window,message,wp,lp);
    }return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK shelf_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_WINDOWPOSCHANGING:if(!shelf.fixture&&!floating_preview())lb::desktop_layer::protect_position(*reinterpret_cast<WINDOWPOS*>(lp),lb::desktop_layer::host());break;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_CREATE:shelf.last_query.clear();lb::desktop_layer::mark(window);shelf.dismiss_after_menu=false;shelf.foreground_cached=false;shelf.painted={};shelf.views={};shelf.window=window;DragAcceptFiles(window,TRUE);shelf.search=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_CLIENTEDGE,L"EDIT",L"",WS_POPUP|ES_AUTOHSCROLL|WS_TABSTOP,0,0,scaled(360),scaled(32),window,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.search,GWLP_ID,1);lb::window_shape::own_frame(shelf.search);SendMessageW(shelf.search,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.search,EM_SETCUEBANNER,TRUE,LPARAM(tr(L"搜索桌面与分组",L"Search desktop and groups")));SetWindowSubclass(shelf.search,edit_keys,2,0);shelf.icons.start(window,WM_APP+93);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint(window);return 0;
    case WM_SIZE:if(!shelf.presenting_geometry){position_edits();InvalidateRect(window,nullptr,FALSE);}return 0;
    case WM_WINDOWPOSCHANGED:if(!shelf.presenting_geometry)position_edits();break;
    case WM_CTLCOLOREDIT:SetBkColor(HDC(wp),g_app.settings.organizer_color);SetTextColor(HDC(wp),foreground(g_app.settings.organizer_color));return LRESULT(shelf.edit_brush);
    case WM_MOUSEMOVE: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(shelf.dragging&&shelf.moving) { POINT cursor;GetCursorPos(&cursor);RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,rect.left+cursor.x-shelf.pressed.x,rect.top+cursor.y-shelf.pressed.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);shelf.pressed=cursor; }
        else if((!shelf.drag_entry.empty()||!shelf.drag_group.empty())&&!shelf.drag_started&&(std::abs(point.x-shelf.pressed.x)>GetSystemMetrics(SM_CXDRAG)||std::abs(point.y-shelf.pressed.y)>GetSystemMetrics(SM_CYDRAG))){shelf.drag_started=true;bool pinned=std::any_of(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.pin&&e.path==shelf.drag_entry;});if(!shelf.drag_entry.empty()&&!pinned){shelf.drag_groups=true;shelf.expanded=true;shelf.group.clear();ShowWindow(shelf.search,SW_HIDE);reflow();UpdateWindow(window);GetCursorPos(&point);ScreenToClient(window,&point);}}
        hover_to(hit(point));TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,window,0};TrackMouseEvent(&track);if(!shelf.mouse_inside)hover_opacity(true);return 0; }
    case WM_MOUSELEAVE:{
        POINT point{};RECT bounds{};GetCursorPos(&point);GetWindowRect(window,&bounds);
        if(!PtInRect(&bounds,point))hover_opacity(false);
        hover_to(-1);return 0;
    }
    case WM_LBUTTONDOWN: { if(shelf.editor)commit_title();if(!shelf.model||shelf.window!=window)return 0;POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int target=hit(point);shelf.pressed_tile=target;InvalidateRect(window,nullptr,FALSE);shelf.drag_group.clear();shelf.drag_entry.clear();shelf.drag_started=false;shelf.pressed=point;shelf.drag_origin=shelf.group;shelf.drag_expanded=shelf.expanded;
        if(shelf.moving){shelf.dragging=true;GetCursorPos(&shelf.pressed);SetCapture(window);}
        else if(target>=0){auto tile=shelf.tiles[size_t(target)];if(tile.group&&tile.index<shelf.model->groups.size())shelf.drag_group=shelf.model->groups[tile.index].id;else if(tile.index<shelf.model->entries.size())shelf.drag_entry=shelf.model->entries[tile.index].path;SetCapture(window);}return 0; }
    case WM_LBUTTONUP: {
        if(shelf.dragging) { shelf.dragging=false;ReleaseCapture();RECT rect{};GetWindowRect(window,&rect);g_app.settings.organizer_custom_position=1;g_app.settings.organizer_x=rect.left;g_app.settings.organizer_y=rect.top;place(shelf.height);GetWindowRect(window,&rect);g_app.settings.organizer_x=rect.left;g_app.settings.organizer_y=rect.top;save_config();return 0; }
        int pressed=std::exchange(shelf.pressed_tile,-1);InvalidateRect(window,nullptr,FALSE);auto source_group=std::exchange(shelf.drag_group,{}),source_entry=std::exchange(shelf.drag_entry,{});bool dragged=std::exchange(shelf.drag_started,false),groups=std::exchange(shelf.drag_groups,false);if(GetCapture()==window)ReleaseCapture();
        if(!shelf.model||shelf.window!=window)return 0;
        int target=hit(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
        if(dragged){
            if(target>=0&&!source_entry.empty()&&!shelf.tiles[size_t(target)].group&&shelf.tiles[size_t(target)].current(*shelf.model)&&shelf.tiles[size_t(target)].index<shelf.model->entries.size()){
                int rank=shelf.model->entries[shelf.tiles[size_t(target)].index].pin;
                if(rank)model_result(shelf.model->set_pin(source_entry,rank));
            }
            if(target>=0&&shelf.tiles[size_t(target)].group&&shelf.tiles[size_t(target)].current(*shelf.model)){auto destination=shelf.tiles[size_t(target)].index;if(!source_entry.empty())model_result(shelf.model->collect(source_entry,shelf.model->groups[destination].id));else if(!source_group.empty()){auto from=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[&](auto const& group){return group.id==source_group;});if(from!=shelf.model->groups.end())model_result(shelf.model->move_group(size_t(from-shelf.model->groups.begin()),destination));}}
            if(groups){shelf.group=shelf.drag_origin;shelf.expanded=shelf.drag_expanded;ShowWindow(shelf.search,shelf.expanded?SW_SHOW:SW_HIDE);}reflow();schedule_collect();return 0;}
        if(target>=0&&target==pressed) { auto const tile=shelf.tiles[size_t(target)];if(!tile.current(*shelf.model)||!tile.matches_press(source_entry,source_group)) return 0;if(tile.index==SIZE_MAX) { shelf.group.clear();animate(true);SetForegroundWindow(window);SetFocus(shelf.search); }else if(tile.group) { shelf.group=shelf.model->groups[tile.index].id;animate(true); }else { launch(shelf.model->entries[tile.index]); } }
        else if(shelf.expanded) { if(!shelf.group.empty()&&GET_Y_LPARAM(lp)<scaled(55)) { auto it=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& g){return g.id==shelf.group;});if(it!=shelf.model->groups.end()) { rename_group(size_t(it-shelf.model->groups.begin()));SetWindowSubclass(shelf.editor,edit_keys,1,0); } }else animate(false); }return 0;
    }
    case WM_CAPTURECHANGED:{shelf.pressed_tile=-1;InvalidateRect(window,nullptr,FALSE);bool changed=shelf.drag_groups;shelf.dragging=false;shelf.drag_group.clear();shelf.drag_entry.clear();shelf.drag_started=false;shelf.drag_groups=false;if(changed){shelf.group=shelf.drag_origin;shelf.expanded=shelf.drag_expanded;ShowWindow(shelf.search,shelf.expanded?SW_SHOW:SW_HIDE);reflow();}schedule_collect();return 0;}
    case WM_CONTEXTMENU: { POINT point;GetCursorPos(&point);POINT local=point;ScreenToClient(window,&local);context_menu(point,hit(local));return 0; }
    case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(wp);scope_exit finish([&]{DragFinish(drop);});POINT point{};DragQueryPoint(drop,&point);std::wstring group=shelf.group;int target=hit(point);if(target>=0&&shelf.tiles[size_t(target)].group)group=shelf.model->groups[shelf.tiles[size_t(target)].index].id;if(group.empty())group=L"other";UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count>10000)return 0;std::vector<std::wstring> paths;for(UINT n=0;n<count;++n){UINT length=DragQueryFileW(drop,n,nullptr,0);if(!length||length>=LB_PATH)return 0;std::wstring path(length+1,L'\0');DragQueryFileW(drop,n,path.data(),length+1);path.resize(length);paths.push_back(std::move(path));}if(model_result(shelf.model->add_paths(paths,group))){rebuild_loose();schedule_collect();reflow();}return 0;}
    case WM_COMMAND:if(LOWORD(wp)==1&&HIWORD(wp)==EN_CHANGE&&HWND(lp)==shelf.search) {
        wchar_t query[260]{};GetWindowTextW(shelf.search,query,260);
        // A native edit notification and the IME fallback can describe the same change.
        if(shelf.last_query!=query){shelf.last_query=query;if(shelf.expanded)reflow();}
    }else if(LOWORD(wp)==2&&HIWORD(wp)==EN_KILLFOCUS&&HWND(lp)==shelf.editor) commit_title();return 0;
    case WM_KEYDOWN:if(wp=='Z'&&GetKeyState(VK_CONTROL)<0&&shelf.model&&shelf.model->can_undo()){
        if(model_result(shelf.model->undo()))PostMessageW(window,WM_APP+95,0,0);return 0;
    }if(wp==VK_ESCAPE) { if(GetCapture()==window){ReleaseCapture();return 0;}shelf.moving=false;animate(false); }return 0;
    case WM_MOUSEWHEEL:if(shelf.expanded) { int columns=std::max(1,(width()-scaled(24))/cell_size());wchar_t query[260]{};GetWindowTextW(shelf.search,query,260);size_t count=g_app.settings.organizer_view==2&&shelf.group.empty()&&!*query?shelf.model->groups.size()+shelf.views.pinned_count(*shelf.model):shelf.views.list(*shelf.model,g_app.settings.organizer_view,query,shelf.group).size();int max_rows=std::max(0,int((count+size_t(columns)-1)/size_t(columns))-std::max(1,(shelf.height-scaled(58)-scaled(6))/row_size()));shelf.scroll=std::clamp(shelf.scroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1),0,max_rows);InvalidateRect(window,nullptr,FALSE); }return 0;
    case WM_TIMER:
        if(wp==1) { auto now=GetTickCount64();double seconds=double(now-shelf.animation)/1000.;bool active=shelf.height_motion.step(seconds,shelf.reduced_motion);active=shelf.header_motion.step(seconds,shelf.reduced_motion)||active;shelf.animation=now;if(!place(int(std::lround(shelf.height_motion.value)),&shelf.layout_work))InvalidateRect(window,nullptr,FALSE);if(!active){KillTimer(window,1);if(!shelf.expanded){RECT r{};GetClientRect(window,&r);shelf.painted={};shelf.foreground_cached=false;shelf.surface.trim(r.right,r.bottom);shelf.content_black.trim(r.right,r.bottom);shelf.content_white.trim(r.right,r.bottom);InvalidateRect(window,nullptr,FALSE);}} }
        else if(wp==7){auto now=GetTickCount64();auto prior=int(shelf.opacity_motion.value);bool active=shelf.opacity_motion.step(double(now-shelf.opacity_tick)/1000.,shelf.reduced_motion);shelf.opacity_tick=now;if(prior!=int(shelf.opacity_motion.value)){
                if(shelf.foreground_cached&&!GetUpdateRect(window,nullptr,FALSE)){
                    RECT client{};GetClientRect(window,&client);render_frame(window,client,client,true);
                }else InvalidateRect(window,nullptr,FALSE);
            }if(!active)KillTimer(window,7);}
        else if(wp==4){float t=std::min(1.f,float(GetTickCount64()-shelf.hover_started)/135.f),ease=1-std::pow(1-t,3);for(auto it=shelf.hovers.begin();it!=shelf.hovers.end();){auto& value=it->second;value.value=value.from+(value.target-value.from)*ease;invalidate_tile(it->first);if(t>=1&&value.target==0)it=shelf.hovers.erase(it);else ++it;}if(t>=1)KillTimer(window,4);}
        else if(wp==2) { KillTimer(window,2);shelf.change_started=0;synchronize(); }
        else if(wp==3) { KillTimer(window,3);if(interaction_busy()||native_dialog_open()){SetTimer(window,3,500,nullptr);return 0;}if(g_app.settings.organizer_auto_collect){auto now=std::chrono::steady_clock::now();bool due=std::any_of(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& entry){return entry.loose&&!entry.manual_loose&&entry.due<=now;});if(shelf.model->collect_due(now)){rebuild_loose();reflow();}else if(due){SetTimer(window,3,2000,nullptr);return 0;}}schedule_collect(); }
        else if(wp==9)finish_scan();
        else if(wp==8){
            if(finish_file_rename()&&shelf.stopping&&shutdown()&&g_app.settings.organizer_enabled) configure();
        }
        else if(wp==5){KillTimer(window,5);if(interaction_busy())SetTimer(window,5,200,nullptr);else {rebuild_loose();schedule_collect();reflow();}}
        else if(wp==6&&!interaction_busy()){auto work=monitor_work();if(!EqualRect(&work,&shelf.layout_work))reflow();if(!shelf.fixture&&!floating_preview()){auto host=lb::desktop_layer::host();lb::desktop_layer::maintain(window,host);for(auto const& item:shelf.loose)lb::desktop_layer::maintain(item->window,host);}}return 0;
    case changed_message:if(wp==shelf.token)desktop_changed();return 0;
    case WM_APP+93:{
        std::set<std::wstring> ready;
        bool retry=shelf.icons.receive(nullptr,&ready);
        if(ready.empty())return 0;
        if(retry||shelf.views.revision!=shelf.model->revision)InvalidateRect(window,nullptr,FALSE);
        else for(auto const& tile:shelf.tiles){
            bool redraw=!tile.group&&tile.index<shelf.model->entries.size()&&ready.contains(icon_key(shelf.model->entries[tile.index]));
            if(tile.group){
                auto const& covers=shelf.views.group_covers(*shelf.model);
                auto cover=covers.find(tile.identity);
                redraw=cover!=covers.end()&&std::any_of(cover->second.begin(),cover->second.end(),
                    [&](size_t index){return ready.contains(icon_key(shelf.model->entries[index]));});
            }
            if(redraw)InvalidateRect(window,&tile.bounds,FALSE);
        }
        for(auto const& item:shelf.loose)if(retry||ready.contains(item->icon_key))InvalidateRect(item->window,nullptr,FALSE);
        return 0;
    }
    case WM_APP+94:if(shelf.expanded&&!interaction_busy()) { commit_title();animate(false); }return 0;
    case WM_APP+95:
        rebuild_loose();schedule_collect();
        if(wp&&shelf.expanded)animate(false);else reflow();
        return 0;
    case WM_DPICHANGED:shelf.dpi=HIWORD(wp);shelf_fonts();reflow();return 0;
    case WM_DISPLAYCHANGE:reflow();return 0;
    case WM_SETTINGCHANGE:if(wp==SPI_SETWORKAREA)reflow();else{appearance(window,scaled(36));if(shelf.reduced_motion){KillTimer(window,1);KillTimer(window,4);KillTimer(window,7);shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.expanded?scaled(48):0);place(shelf.target);}}return 0;
    case WM_CLOSE:if(shelf.preview) shutdown();return 0;
    case WM_DESTROY:RemovePropW(window,lb::desktop_layer::surface_property);if(shelf.scan){shelf.scan->cancelled=true;shelf.scan.reset();}KillTimer(window,9);shelf.views={};shelf.painted={};for(auto timer:{1,2,3,4,5,6,7,8})KillTimer(window,timer);if(shelf.search)DestroyWindow(std::exchange(shelf.search,nullptr));if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.surface.clear();shelf.content_black.clear();shelf.content_white.clear();shelf.plates.clear();shelf.search_alpha=-1;if(shelf.edit_brush)DeleteObject(std::exchange(shelf.edit_brush,nullptr));shelf.icons.stop();if(shelf.outside) UnhookWindowsHookEx(std::exchange(shelf.outside,nullptr));shelf.window=nullptr;shelf.search=nullptr;shelf.editor=nullptr;shelf.editor_anchor=nullptr;shelf.editing_file=false;shelf.editing_entry.clear();shelf.drag_entry.clear();shelf.drag_group.clear();shelf.drag_groups=shelf.drag_started=shelf.dragging=shelf.menu=false;for(auto font:{shelf.font,shelf.small,shelf.heading})if(font)DeleteObject(font);shelf.font=shelf.small=shelf.heading=nullptr;shelf.hovers.clear();return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
bool show_shelf(std::vector<RootListing> const* initial=nullptr) {
    wchar_t layer_fixture[2]{};shelf.layer_preview=shelf.preview&&GetEnvironmentVariableW(L"LIGHTBLOCK_ORGANIZER_LAYER_FIXTURE",layer_fixture,2)&&layer_fixture[0]==L'1';
    ++shelf.generation;
    shelf.model=std::make_unique<Model>(g_app.data);
    if(!shelf.model->load()) {
        set_status(L"无法读取收纳配置，已保留原桌面与配置文件。",L"Could not read the organizer settings. Your desktop and settings are preserved.");
        return false;
    }
    if(shelf.preview){auto root=std::filesystem::path(g_app.data)/L"sample-desktop";std::error_code error;for(auto name:{L"Documents",L"Pictures",L"Projects",L"Music",L"Videos",L"Notes",L"Archive"}){std::filesystem::create_directories(root/name,error);if(error)return false;}shelf.roots={root.wstring()};}
    bool protect=GetTickCount64()<shelf.protect_empty_until;
    bool loaded=initial?shelf.model->apply_scan(*initial,false,protect):shelf.model->synchronize(shelf.roots,false,protect);
    if(!loaded)return false;
    shelf.dpi=GetDpiForSystem();shelf_fonts();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpfnWndProc=shelf_proc;cls.lpszClassName=L"LightBlockDesktopNative.Organizer";RegisterClassW(&cls);cls.style=CS_DBLCLKS;cls.lpfnWndProc=loose_proc;cls.lpszClassName=L"LightBlockDesktopNative.LooseIcon";RegisterClassW(&cls);
    HWND window=CreateWindowExW((floating_preview()?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_LAYERED,L"LightBlockDesktopNative.Organizer",tr(L"轻块桌面 · 收纳",L"Light Block Desktop · Organizer"),WS_POPUP,0,0,scaled(1050),base_height(),floating_preview()?nullptr:GetShellWindow(),nullptr,g_app.instance,nullptr);
    if(!window) return false;
    if(UINT dpi=GetDpiForWindow(window);dpi&&dpi!=shelf.dpi){shelf.dpi=dpi;shelf_fonts();}
    shelf.enabled=true;shelf.expanded=false;shelf.mouse_inside=false;shelf.header_motion.reset(0);shelf.height_motion.reset(base_height());shelf.group.clear();appearance(window,scaled(36));place(base_height());SetTimer(window,6,750,nullptr);rebuild_loose();schedule_collect();arrival_anchor.valid=false;if(!shelf.fixture&&!shelf.outside)shelf.outside=SetWindowsHookExW(WH_MOUSE_LL,outside_click,g_app.instance,0);shelf.watcher.start(shelf.roots,window,changed_message,shelf.token);desktop_changed();return true;
}
bool can_show_shelf(){
    return g_app.settings.organizer_enabled&&!g_app.close_requested&&!shelf.stopping&&!shelf.session_suspended&&shelf.ready;
}
void acknowledge_shelf(){
    if(!shelf.fixture)UpdateWindow(shelf.window);
    SetEvent(shelf.ready.get());shelf.retry_seconds=0;
    SetTimer(g_app.window,17,1500,nullptr);
}
bool start_lease(bool restore_only) {
    auto ready_name=L"Local\\LightBlockNative.ShelfReady."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(shelf.token+1);
    shelf.ready.reset(CreateEventW(nullptr,TRUE,FALSE,ready_name.c_str()));
    if(!shelf.ready) return false;
    shelf.recovering=restore_only;
    auto stop_name=L"Local\\LightBlockNative.IconStop."+std::to_wstring(GetCurrentProcessId());shelf.stop.reset(CreateEventW(nullptr,TRUE,FALSE,stop_name.c_str()));if(!shelf.stop) return false;ResetEvent(shelf.stop.get());
    FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return false;ULONGLONG timestamp=(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    wchar_t exe[LB_PATH*2],data[LB_PATH*2];if(!quote_arg(exe,ARRAYSIZE(exe),g_app.exe)||!quote_arg(data,ARRAYSIZE(data),g_app.data)) return false;
    auto command=std::wstring(exe)+L" --icon-lease --owner "+std::to_wstring(uintptr_t(g_app.window))+L" --token "+std::to_wstring(++shelf.token)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --parent-created "+std::to_wstring(timestamp)+L" --data-dir "+data+(restore_only?L" --restore-only":L"");
    STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION process{};if(!CreateProcessW(g_app.exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&process)) return false;CloseHandle(process.hThread);shelf.lease.reset(process.hProcess);SetTimer(g_app.window,17,100,nullptr);return true;
}
void retry_after_shell_settles() {
    if(shelf.session_suspended||g_app.close_requested||(!g_app.settings.organizer_enabled&&!shelf.recovering)) return;
    shelf.retry_seconds=std::min(60u,std::max(2u,shelf.retry_seconds*2));
    shelf.retry_pending=true;
    SetTimer(g_app.window,17,shelf.retry_seconds*1000,nullptr);
    log_line(L"Desktop unavailable; retrying in %u seconds",shelf.retry_seconds);
}
}
bool configure(bool preview) {
    if(shelf.session_suspended||g_app.close_requested) return true;
    shelf.retry_pending=false;
    if(!shelf.protect_empty_until) shelf.protect_empty_until=GetTickCount64()+90000;
    preview=preview||shelf.preview;
    if(preview&&!g_app.settings.organizer_enabled){shelf.preview=true;return shutdown();}
    if(shelf.lease&&WaitForSingleObject(shelf.lease.get(),0)!=WAIT_OBJECT_0) { if(!g_app.settings.organizer_enabled&&!shelf.preview) shutdown();else if(shelf.window) { reflow();rebuild_loose();appearance(shelf.window,scaled(26));schedule_collect();InvalidateRect(shelf.window,nullptr,FALSE); }return true; }
    shelf.stopping=false;
    if(shelf.window) { if(!g_app.settings.organizer_enabled&&!preview) return shutdown();reflow();rebuild_loose();appearance(shelf.window,scaled(26));schedule_collect();InvalidateRect(shelf.window,nullptr,FALSE);return true; }
    shelf.preview=preview;if(preview) return show_shelf();
    std::error_code error;
    bool restore=std::filesystem::exists(std::filesystem::path(g_app.data)/L"native-desktop-icons.json",error);
    if(!g_app.settings.organizer_enabled&&!restore) return true;
    if(start_lease(!g_app.settings.organizer_enabled)) return true;
    retry_after_shell_settles();return false;
}
bool has_session() { return bool(shelf.lease); }
void suspend_for_session_end() {
    if(shelf.session_suspended) return;
    shelf.session_suspended=true;
    // Let Explorer restore its own flag even if a shell rename is still busy.
    if(shelf.stop) SetEvent(shelf.stop.get());
    shutdown();
    if(shelf.lease && WaitForSingleObject(shelf.lease.get(),900)==WAIT_TIMEOUT)
        log_line(L"Session query: desktop restoration still pending");
}
void resume_after_cancelled_session() {
    if(!std::exchange(shelf.session_suspended,false)||g_app.close_requested) return;
    if(shelf.lease) tick(17);
    else if(!shelf.rename) configure();
}
bool shutdown() {
    if(shelf.startup_scan){shelf.startup_scan->cancelled=true;shelf.startup_scan.reset();}
    shelf.retry_pending=false;
    commit_title();if(!finish_file_rename()){shelf.stopping=true;return false;}
    ++shelf.generation;
    shelf.stopping=true;shelf.enabled=false;commit_title();shelf.watcher.stop();for(auto &item:shelf.loose) if(item->window) DestroyWindow(item->window);shelf.loose.clear();if(shelf.window) DestroyWindow(shelf.window);shelf.window=nullptr;shelf.search=nullptr;shelf.editor=nullptr;if(shelf.stop) SetEvent(shelf.stop.get());
    shelf.model.reset();shelf.tiles.clear();shelf.roots.clear();shelf.change_started=0;
    if(shelf.lease&&WaitForSingleObject(shelf.lease.get(),0)!=WAIT_OBJECT_0) { if(g_app.window) SetTimer(g_app.window,17,100,nullptr);return false; }
    shelf.lease.reset();shelf.stop.reset();shelf.ready.reset();return true;
}
void tick(UINT timer) {
    if(timer!=17) return;if(!shelf.lease) {
        KillTimer(g_app.window,17);
        if(std::exchange(shelf.retry_pending,false)) configure();
        return;
    }
    if(WaitForSingleObject(shelf.lease.get(),0)==WAIT_OBJECT_0) {
        if(shelf.rename&&!finish_file_rename()) return;
        DWORD code=0;GetExitCodeProcess(shelf.lease.get(),&code);shelf.lease.reset();shelf.stop.reset();KillTimer(g_app.window,17);
        shelf.ready.reset();
        bool restart=shelf.stopping, recover=shelf.recovering;
        shutdown();
        shelf.stopping=false;
        if(shelf.session_suspended||g_app.close_requested) return;
        if(restart&&g_app.settings.organizer_enabled) configure();
        else if(g_app.settings.organizer_enabled||(recover&&code)) retry_after_shell_settles();
    }else if(shelf.startup_scan&&shelf.startup_scan->done){
        auto scan=std::move(shelf.startup_scan);
        // Explorer keeps its icons visible until a complete shelf is ready.
        if(can_show_shelf()&&show_shelf(&scan->listing))acknowledge_shelf();
        else if(shelf.stop)SetEvent(shelf.stop.get());
    }
}
void desktop_changed() { if(shelf.window){++shelf.change_serial;auto now=GetTickCount64();if(!shelf.change_started)shelf.change_started=now;SetTimer(shelf.window,2,now-shelf.change_started>=1000?1:250,nullptr);} }
void shell_restarted() {
    if(shelf.preview||!g_app.settings.organizer_enabled||g_app.close_requested||shelf.session_suspended) return;
    shelf.protect_empty_until=GetTickCount64()+90000;
    // Retire the old IFolderView lease before claiming the replacement Explorer
    // desktop. Its durable receipt keeps the user's original icon visibility.
    // tick() restarts after the old helper has acknowledged restoration.
    if(shutdown()) {shelf.stopping=false;configure();}
}
namespace {
std::unique_ptr<Model> settings_model;
Model* editable_model(bool reload) {
    if(shelf.window&&shelf.model)return shelf.model.get();
    if(reload||!settings_model){auto next=std::make_unique<Model>(g_app.data);if(!next->load())return nullptr;settings_model=std::move(next);}return settings_model.get();
}
}
int settings_group(int index,UiGroup* out) {
    if(index<0||!out)return 0;auto model=editable_model(index==0);if(!model||size_t(index)>=model->groups.size())return 0;auto const& group=model->groups[size_t(index)];*out={};StringCchCopyW(out->id,104,group.id.c_str());StringCchCopyW(out->name,128,group.name.c_str());StringCchCopyW(out->patterns,241,group.patterns.c_str());out->color=int(group.color);out->count=int(std::count_if(model->entries.begin(),model->entries.end(),[&](auto const& e){return e.group==group.id;}));return 1;
}
int settings_group_action(int action,wchar_t const* identifier,wchar_t const* argument,int value) {
    auto model=editable_model(true);if(!model)return 0;std::wstring id=identifier?identifier:L"",arg=argument?argument:L"";if(id.size()>100||arg.size()>=LB_PATH)return 0;auto before=*model;bool ok=false;
    auto found=std::find_if(model->groups.begin(),model->groups.end(),[&](auto const& group){return group.id==id;});
    if(action==7)return model->set_patterns(id,arg)?1:0;
    if(action==0&&arg.size()<=120&&model->groups.size()<128){model->create_group(arg);ok=true;}
    else if(action==4){ok=model->synchronize(desktop_roots(),false);}
    else if(found!=model->groups.end()) {
        if(action==1&&arg.size()<=120){found->name=arg;found->automatic=false;ok=true;}
        else if(action==2&&value>=0&&value<=0xffffff){found->color=COLORREF(value);ok=true;}
        else if(action==3&&id!=L"other")ok=model->remove_group(id);
        else if(action==5||action==6){
            bool system=action==6;DWORD attributes=system?0:GetFileAttributesW(arg.c_str());if((system&&(arg==L"shell:MyComputerFolder"||arg==L"shell:RecycleBinFolder"||arg==L"ms-settings:"))||(!system&&attributes!=INVALID_FILE_ATTRIBUTES&&!PathIsRelativeW(arg.c_str()))) {
                auto entry=std::find_if(model->entries.begin(),model->entries.end(),[&](auto const& e){return !_wcsicmp(e.path.c_str(),arg.c_str());});
                if(entry!=model->entries.end()){ok=model->collect(entry->path,id);}else if(model->entries.size()<10000){Entry item;item.path=arg;item.name=system?(arg==L"shell:MyComputerFolder"?tr(L"此电脑",L"This PC"):arg==L"shell:RecycleBinFolder"?tr(L"回收站",L"Recycle Bin"):tr(L"Windows 设置",L"Windows settings")):PathFindFileNameW(arg.c_str());item.group=id;item.system=system;item.external=true;item.directory=!system&&(attributes&FILE_ATTRIBUTE_DIRECTORY);model->entries.push_back(std::move(item));ok=true;}
            }
        }
    }
    if(ok&&action!=3&&action!=4)ok=model->save();if(!ok){*model=std::move(before);set_status(L"分组更改未保存，请重试；原文件保持不变。",L"Group change could not be saved. Files remain unchanged.");return 0;}
    if(shelf.window){if(!shelf.group.empty()&&!std::any_of(model->groups.begin(),model->groups.end(),[](auto const& g){return g.id==shelf.group;}))shelf.group.clear();rebuild_loose();schedule_collect();InvalidateRect(shelf.window,nullptr,FALSE);}return 1;
}
bool message(UINT msg,WPARAM wp,LPARAM lp) {
    if(msg!=WM_APP+90&&msg!=WM_APP+91) return false;if(wp!=shelf.token||!shelf.lease) return true;
    if(msg==WM_APP+90) {
        if(!can_show_shelf()){
            if(shelf.stop)SetEvent(shelf.stop.get());
        }else if(shelf.window)acknowledge_shelf();
        else if(!shelf.startup_scan){
            shelf.roots=desktop_roots();
            shelf.startup_scan=begin_scan(shelf.roots);
            if(!shelf.startup_scan&&shelf.stop)SetEvent(shelf.stop.get());
        }
    }
    else {
        if(shelf.startup_scan){shelf.startup_scan->cancelled=true;shelf.startup_scan.reset();}
        if(shelf.stop) SetEvent(shelf.stop.get());
        // This helper may belong to an earlier off -> on transition. Its result
        // must never replace the user's latest desired state.
        if(shelf.stopping||g_app.close_requested||shelf.session_suspended) return true;
        log_line(L"Organizer helper reported 0x%08lX",DWORD(lp));
        set_status(L"正在等待 Windows 桌面就绪，将自动重试。",L"Waiting for the Windows desktop. Will retry automatically.");
    }return true;
}
bool settings_group_tests(std::wstring const& directory) {
    if(shelf.window||shelf.lease)return false;
    auto previous=std::wstring(g_app.data);auto cached=std::move(settings_model);
    scope_exit restore([&]{StringCchCopyW(g_app.data,LB_PATH,previous.c_str());settings_model=std::move(cached);});
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;
    StringCchCopyW(g_app.data,LB_PATH,directory.c_str());
    if(!settings_group_action(0,L"",L"预览分组",0))return false;
    UiGroup group{},created{};for(int n=0;settings_group(n,&group);++n)if(!wcscmp(group.name,L"预览分组"))created=group;
    if(!created.id[0]||!settings_group_action(1,created.id,L"",0)||!settings_group_action(2,created.id,L"",RGB(80,120,220)))return false;
    auto file=std::filesystem::path(directory)/L"保留原文件.txt";if(!write_utf8_atomic(file.c_str(),L"preserve original file"))return false;
    if(!settings_group_action(5,created.id,file.c_str(),0))return false;
    bool saved=false;for(int n=0;settings_group(n,&group);++n)if(!wcscmp(group.id,created.id))saved=!group.name[0]&&group.color==int(RGB(80,120,220))&&group.count==1;
    if(!saved||settings_group_action(5,created.id,L"relative.txt",0)||settings_group_action(2,created.id,L"",-1))return false;
    if(!settings_group_action(3,created.id,L"",0)||!std::filesystem::exists(file,error))return false;
    for(int n=0;settings_group(n,&group);++n)if(!wcscmp(group.id,created.id))return false;
    return !settings_group_action(3,L"other",L"",0);
}
// Compare localized animation frames with a full repaint of exactly the same
// state. This uses an offscreen layered HWND; no screen capture is involved.
static bool hover_render_tests(HWND window){
    shelf.dpi=144;shelf_fonts();g_app.settings.organizer_icon_size=1;
    g_app.settings.icon_opacity=35;g_app.settings.organizer_glass=1;
    shelf.opacity_motion.reset(175);shelf.expanded=true;
    scope_exit cleanup([&]{shelf.expanded=false;shelf.hovers.clear();});
    auto& model=*shelf.model;
    for(int i=0;i<200;++i){
        auto number=std::to_wstring(i);
        model.entries.push_back({L"fixture-"+number,L"Item "+number,model.groups[size_t(i)%model.groups.size()].id,0,true});
    }
    ++model.revision;shelf.header_motion.reset(scaled(48));
    RECT client{0,0,scaled(990),scaled(510)};
    SetWindowPos(window,nullptr,-32000,-32000,client.right,client.bottom,SWP_NOACTIVATE|SWP_NOZORDER);
    SetWindowTextW(shelf.search,L"");
    auto pixels=[&]{
        std::vector<uint32_t> result;
        for(int y=0;y<client.bottom;++y){
            auto row=shelf.surface.pixels()+size_t(y)*shelf.surface.stride();
            result.insert(result.end(),row,row+client.right);
        }
        return result;
    };
    for(int order:{0,2}){
        g_app.settings.organizer_view=order;
        render_frame(window,client,client);
        if(shelf.tiles.size()<4||shelf.painted.cx!=client.right)return false;
        // Freeze icon availability for both paths being compared.
        shelf.icons.receive();
        render_frame(window,client,client);
        shelf.opacity_motion.reset(190);
        render_frame(window,client,client,true);
        auto cached_opacity=pixels();
        render_frame(window,client,client);
        if(cached_opacity!=pixels())return false;
        auto unchanged=pixels();
        auto bounds=shelf.tiles[2].bounds;
        shelf.hovers[2].value=1;
        render_frame(window,client,bounds);
        auto localized=pixels();
        render_frame(window,client,client);
        if(localized!=pixels()||(!order&&localized==unchanged))return false;
        std::vector<double> full,local;
        for(int i=0;i<40;++i){
            auto time=[&](RECT area){
                auto before=std::chrono::steady_clock::now();render_frame(window,client,area);
                return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
            };
            shelf.hovers[2].value=(i%10)/10.f;
            if(i%2){full.push_back(time(client));local.push_back(time(bounds));}
            else{local.push_back(time(bounds));full.push_back(time(client));}
        }
        std::sort(full.begin(),full.end());std::sort(local.begin(),local.end());
        log_line(L"Organizer offscreen hover (200 added entries, 144 DPI) view=%d full_p95_ms=%.2f tile_p95_ms=%.2f",order,full[37],local[37]);
        shelf.hovers.clear();
    }
    auto& views=shelf.views;
    auto const& before=views.group_covers(model);
    auto id=model.groups[1].id;
    auto previous=before.at(id).front();
    model.entries[previous].loose=true;++model.revision;
    auto const& after=views.group_covers(model).at(id);
    if(after.empty()||after.front()==previous)return false;
    auto path=model.entries[previous].name;
    auto const& search=views.list(model,0,path,id);
    if(std::find(search.begin(),search.end(),previous)!=search.end())return false;
    return true;
}
bool surface_tests(std::wstring const& directory){
    if(shelf.window||shelf.lease||shelf.model)return false;
    auto previous=g_app.settings;auto dpi=shelf.dpi;
    scope_exit restore([&]{if(shelf.window)DestroyWindow(shelf.window);shelf.model.reset();shelf.tiles.clear();shelf.dpi=dpi;g_app.settings=previous;});
    shelf.model=std::make_unique<Model>(directory);shelf.model->initialize_groups();shelf.model->entries.push_back({L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",L"Icon opacity fixture",shelf.model->groups.front().id,0,true});shelf.dpi=96;shelf_fonts();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=shelf_proc;cls.lpszClassName=L"LightBlockDesktopNative.OrganizerSurfaceTest";RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,cls.lpszClassName,L"Isolated alpha surface",WS_POPUP,-32000,-32000,500,140,nullptr,nullptr,g_app.instance,nullptr);if(!window)return false;
    g_app.settings.organizer_color=RGB(40,90,120);g_app.settings.organizer_opacity=210;appearance(window,36);
    for(int view:{0,2})for(int size:{0,1,2})for(int align:{0,1,2}){
        g_app.settings.organizer_view=view;
        g_app.settings.organizer_icon_size=size;g_app.settings.organizer_alignment=align;
        int height=base_height(),width=600;SetWindowPos(window,nullptr,-32000,-32000,width,height,SWP_NOACTIVATE|SWP_NOZORDER);SetWindowTextW(shelf.search,L"");
        for(int opacity:{40,160,255}){
        std::vector<std::pair<size_t,uint32_t>> opaque_foreground;
        for(int plate_opacity:{0,35,100}){
        g_app.settings.icon_opacity=plate_opacity;shelf.opacity_motion.reset(opacity);paint(window);
        if(IsWindowVisible(window)||IsWindowVisible(shelf.search)||!shelf.surface.pixels())return false;
        auto p=shelf.surface.pixels();int stride=shelf.surface.stride();if(p[0]||p[width-1]||p[size_t(height-1)*stride]||p[size_t(height-1)*stride+width-1])return false;
        if((p[size_t(height-3)*stride+width/2]>>24)!=unsigned(opacity))return false;
        auto m=metrics();int plate_x=scaled(12)+m.cell_width/2+m.tile/2-scaled(3),plate_y=scaled(15)+m.tile/2;
        unsigned actual=p[size_t(plate_y)*stride+plate_x]>>24,alpha=(plate_opacity*255+50)/100,expected=alpha+unsigned(opacity)*(255-alpha)/255;
        if(std::abs(int(actual)-int(expected))>2)return false;
        for(auto [index,color]:opaque_foreground)if(p[index]!=color)return false;
        int solid=0;for(int y=0;y<height;++y){for(int x=0;x<width;++x){auto pixel=p[size_t(y)*stride+x],a=pixel>>24;if((pixel&255)>a||((pixel>>8)&255)>a||((pixel>>16)&255)>a)return false;
            if(opacity<255&&lb::desktop_surface::coverage(x,y,width,height,float(scaled(18)))==255){auto b=shelf.content_black.pixels()[size_t(y)*shelf.content_black.stride()+x]&0xffffffu,w=shelf.content_white.pixels()[size_t(y)*shelf.content_white.stride()+x]&0xffffffu;if(b==w){++solid;if(pixel!=(b|0xff000000u))return false;if(!plate_opacity)opaque_foreground.push_back({size_t(y)*stride+x,pixel});}}
        }}
        if(opacity<255&&solid<100)return false;
        }}
        RECT owner{},edit{};GetWindowRect(window,&owner);GetWindowRect(shelf.search,&edit);if(edit.left<owner.left||edit.right>owner.right||edit.top<owner.top||edit.bottom>owner.bottom)return false;
        SetWindowTextW(shelf.search,L"中文 search");wchar_t text[40]{};GetWindowTextW(shelf.search,text,40);if(wcscmp(text,L"中文 search"))return false;
    }
    if(!hover_render_tests(window))return false;
    HWND search=shelf.search;DestroyWindow(window);return !shelf.window&&!IsWindow(search)&&!shelf.surface.pixels();
}
bool arrival_tests(std::wstring const& directory){
    if(shelf.window||shelf.lease||shelf.model||!shelf.loose.empty())return false;
    auto settings=g_app.settings;auto dpi=shelf.dpi;bool fixture=shelf.fixture;shelf.fixture=true;
    scope_exit restore([&]{shelf.watcher.stop();for(auto& item:shelf.loose)if(item->window)DestroyWindow(item->window);shelf.loose.clear();if(shelf.window)DestroyWindow(shelf.window);shelf.model.reset();shelf.roots.clear();shelf.tiles.clear();shelf.fixture=fixture;shelf.change_started=0;shelf.dpi=dpi;g_app.settings=settings;});
    auto root=std::filesystem::path(directory),desktop=root/L"Desktop";std::error_code error;std::filesystem::create_directories(desktop,error);if(error)return false;
    shelf.roots={desktop.wstring()};shelf.model=std::make_unique<Model>(root);if(!shelf.model->synchronize(shelf.roots,false))return false;
    shelf.dpi=96;shelf_fonts();g_app.settings.organizer_view=0;g_app.settings.organizer_icon_size=1;g_app.settings.organizer_auto_collect=1;
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=shelf_proc;cls.lpszClassName=L"LightBlockDesktopNative.ArrivalTest";RegisterClassW(&cls);
    cls.lpfnWndProc=loose_proc;cls.lpszClassName=L"LightBlockDesktopNative.LooseIcon";RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LightBlockDesktopNative.ArrivalTest",L"Hidden arrival fixture",WS_POPUP,0,0,500,140,nullptr,nullptr,g_app.instance,nullptr);if(!window)return false;
    appearance(window,36);reflow();RECT before{};GetWindowRect(window,&before);
    if(!shelf.watcher.start(shelf.roots,window,changed_message,shelf.token))return false;
    ArrivalAnchor expired;expired.capture({-300,200},100);if(!expired.recent(30100)||expired.recent(30101)||expired.recent(99))return false;
    auto work=monitor_work();POINT anchor{work.left+(work.right-work.left)/2,work.top+100};arrival_anchor.capture(anchor,GetTickCount64());
    auto file=desktop/L"New document.txt";if(!write_utf8_atomic(file.c_str(),L"new desktop document"))return false;
    auto until=GetTickCount64()+4000;while(shelf.loose.empty()&&GetTickCount64()<until){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}
    auto find=[&]()->Entry*{auto it=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==file.wstring();});return it==shelf.model->entries.end()?nullptr:&*it;};
    if(shelf.loose.size()!=1||!find()||!find()->loose||!shelf.loose[0]->hint||IsWindowVisible(window)||IsWindowVisible(shelf.loose[0]->window))return false;
    if(!find()->positioned||find()->position.x!=anchor.x-std::max(scaled(184),cell_size())/2||find()->position.y!=anchor.y-scaled(5)||arrival_anchor.valid)return false;
    auto revision=shelf.model->revision;
    auto listing=scan_roots(shelf.roots);
    if(listing.empty())return false;
    std::reverse(listing.front().files.begin(),listing.front().files.end());
    apply_listing(listing);
    if(shelf.model->revision!=revision||!find())return false;
    auto due=find()->due;if(due<std::chrono::steady_clock::now()+std::chrono::seconds(50))return false;
    apply_listing(scan_roots(shelf.roots));if(!find()||find()->due!=due||!find()->loose)return false;
    shelf_proc(window,WM_TIMER,3,0);if(!find()->loose)return false;
    g_app.settings.organizer_auto_collect=0;find()->due=std::chrono::steady_clock::now()-std::chrono::seconds(1);
    shelf_proc(window,WM_TIMER,3,0);if(!find()->loose)return false;
    g_app.settings.organizer_auto_collect=1;schedule_collect();shelf_proc(window,WM_TIMER,3,0);
    RECT after{};GetWindowRect(window,&after);if(find()->loose||!shelf.loose.empty()||after.right-after.left<=before.right-before.left)return false;
    animate(true);SetWindowTextW(shelf.search,L"New document");reflow();paint(window);
    auto found_tile=std::find_if(shelf.tiles.begin(),shelf.tiles.end(),[&](auto const& t){return t.identity==file.wstring();});
    if(found_tile==shelf.tiles.end())return false;
    auto real_popup=popup_menu;
    popup_menu=+[](HMENU menu,UINT,int,int,int,HWND,RECT const*)->BOOL{
        return GetMenuState(menu,23,MF_BYCOMMAND)!=UINT(-1)?23:0;
    };
    context_menu({},int(found_tile-shelf.tiles.begin()));popup_menu=real_popup;
    MSG refresh{};
    if(!PeekMessageW(&refresh,window,WM_APP+95,WM_APP+95,PM_REMOVE)||refresh.wParam!=1)return false;
    DispatchMessageW(&refresh);
    shelf.animation=GetTickCount64()-300;shelf_proc(window,WM_TIMER,1,0);
    if(shelf.expanded||GetWindowTextLengthW(shelf.search)||IsWindowVisible(shelf.search)||shelf.height!=base_height())return false;
    shelf_proc(window,WM_TIMER,3,0);
    if(!find()->loose||!find()->manual_loose||shelf.loose.size()!=1||!std::filesystem::exists(file))return false;
    auto loose=shelf.loose[0]->window;auto bubble=hint_bounds(loose);auto click=MAKELPARAM((bubble.left+bubble.right)/2,(bubble.top+bubble.bottom)/2);
    loose_proc(loose,WM_MOUSEMOVE,0,click);if(!shelf.loose[0]->hint_hover)return false;
    loose_proc(loose,WM_LBUTTONDOWN,0,click);if(!interaction_busy())return false;
    loose_proc(loose,WM_MOUSELEAVE,0,0);loose_proc(loose,WM_LBUTTONUP,0,MAKELPARAM(1,1));if(!find()->loose)return false;
    loose_proc(loose,WM_MOUSEMOVE,0,click);loose_proc(loose,WM_LBUTTONDOWN,0,click);loose_proc(loose,WM_LBUTTONUP,0,click);
    if(find()->loose||!std::filesystem::exists(file))return false;rebuild_loose();
    if(!shelf.loose.empty()||!shelf.model->restore_entry(file.wstring()))return false;rebuild_loose();
    auto prior=*find();if(!start_file_rename(file.wstring(),L"Renamed document.txt"))return false;
    while(shelf.rename&&!shelf.rename->done)Sleep(5);if(!finish_file_rename())return false;file=desktop/L"Renamed document.txt";
    if(!find()||find()->group!=prior.group||!find()->manual_loose||!std::filesystem::exists(file)||std::filesystem::exists(desktop/L"New document.txt"))return false;
    auto collision=desktop/L"Existing.txt";if(!write_utf8_atomic(collision.c_str(),L"keep"))return false;if(!start_file_rename(file.wstring(),L"Existing.txt"))return false;
    while(shelf.rename&&!shelf.rename->done)Sleep(5);bool rejected=!shelf.rename->ok;finish_file_rename();
    if(!rejected||!std::filesystem::exists(file)||!std::filesystem::exists(collision)||start_file_rename(file.wstring(),L"../outside.txt"))return false;
    auto link=desktop/L"Demo.url", renamed=desktop/L"Approved.url";
    if(!write_utf8_atomic(link.c_str(),L"[InternetShortcut]\r\nURL=about:blank\r\n"))return false;
    apply_listing(scan_roots(shelf.roots));
    auto edit=[&](wchar_t const* text){
        shelf.editor=CreateWindowExW(0,L"EDIT",text,WS_CHILD,0,0,100,24,window,HMENU(2),g_app.instance,nullptr);
        if(!shelf.editor)return false;
        shelf.editing_entry=link.wstring();shelf.editing_file=true;
        return SetWindowSubclass(shelf.editor,edit_keys,1,0)!=FALSE;
    };
    if(!edit(L"Unconfirmed"))return false;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(2,EN_KILLFOCUS),LPARAM(shelf.editor));
    if(shelf.editor||shelf.rename||!std::filesystem::exists(link))return false;
    if(!edit(L"Cancelled"))return false;
    SendMessageW(shelf.editor,WM_KEYDOWN,VK_ESCAPE,0);
    if(shelf.editor||shelf.rename||!std::filesystem::exists(link))return false;
    if(!edit(L"Approved"))return false;
    SendMessageW(shelf.editor,WM_KEYDOWN,VK_RETURN,0);
    if(!shelf.rename)return false;
    while(!shelf.rename->done)Sleep(5);
    bool approved=shelf.rename->ok;finish_file_rename();
    return approved&&std::filesystem::exists(renamed)&&!std::filesystem::exists(link)&&
        !std::filesystem::exists(desktop/L"Approved")&&shortcut_extension(L"DEMO.LNK",false)==L".LNK"&&shortcut_extension(L"DEMO.LNK",true).empty();
}
static bool startup_scan_test(){
    auto directory=std::filesystem::path(g_app.data)/L"startup-scan";
    auto desktop=directory/L"Desktop";
    std::error_code error;std::filesystem::create_directories(desktop,error);
    if(error||!write_utf8_atomic((desktop/L"Visible.txt").c_str(),L"fixture"))return false;
    auto data=std::wstring(g_app.data);auto previous_window=g_app.window;
    auto fixture=shelf.fixture;auto stopping=shelf.stopping;auto enabled=shelf.enabled;
    auto roots=shelf.roots;auto dpi=shelf.dpi;
    scope_exit restore([&]{
        shelf.watcher.stop();if(shelf.window)DestroyWindow(shelf.window);
        shelf.startup_scan.reset();shelf.model.reset();shelf.ready.reset();shelf.lease.reset();shelf.stop.reset();
        shelf.fixture=fixture;shelf.stopping=stopping;shelf.enabled=enabled;shelf.roots=std::move(roots);shelf.dpi=dpi;
        if(g_app.window)DestroyWindow(g_app.window);g_app.window=previous_window;
        StringCchCopyW(g_app.data,LB_PATH,data.c_str());
    });
    g_app.window=CreateWindowExW(0,L"STATIC",L"Organizer handshake fixture",0,0,0,1,1,HWND_MESSAGE,nullptr,g_app.instance,nullptr);
    StringCchCopyW(g_app.data,LB_PATH,directory.c_str());
    shelf.fixture=true;shelf.stopping=false;
    shelf.ready.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));shelf.stop.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    shelf.lease.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));shelf.roots={desktop.wstring()};
    if(!shelf.ready||!shelf.stop||!shelf.lease||!g_app.window)return false;
    auto wait=[](auto const& job){
        auto until=GetTickCount64()+3000;
        while(job&&!job->done&&GetTickCount64()<until)Sleep(2);
        return job&&job->done;
    };
    shelf.startup_scan=begin_scan({(directory/L"Offline").wstring()});
    if(!wait(shelf.startup_scan))return false;
    tick(17);
    if(shelf.window||WaitForSingleObject(shelf.ready.get(),0)!=WAIT_TIMEOUT||
       WaitForSingleObject(shelf.stop.get(),0)!=WAIT_OBJECT_0)return false;
    for(int cycle=0;cycle<3;++cycle){
        ResetEvent(shelf.stop.get());ResetEvent(shelf.ready.get());shelf.stopping=false;
        shelf.roots={desktop.wstring()};shelf.startup_scan=begin_scan(shelf.roots);
        if(!wait(shelf.startup_scan))return false;
        tick(17);
        if(!shelf.window||IsWindowVisible(shelf.window)||shelf.outside||
           WaitForSingleObject(shelf.ready.get(),0)!=WAIT_OBJECT_0)return false;
        if(!std::any_of(shelf.model->entries.begin(),shelf.model->entries.end(),[](auto const& e){return e.name==L"Visible.txt"&&!e.loose;}))return false;
        shutdown(); // The fake lease stays alive until its owner acknowledges stop.
        if(shelf.window||shelf.model||shelf.scan||shelf.surface.pixels()||WaitForSingleObject(shelf.stop.get(),0)!=WAIT_OBJECT_0)return false;
    }
    shelf.startup_scan=begin_scan({desktop.wstring()});auto interrupted=shelf.startup_scan;
    shutdown();
    return interrupted&&interrupted->cancelled&&!shelf.startup_scan&&!shelf.window;
}
bool transition_tests() {
    if(shelf.lease||shelf.window||shelf.stop) return false;auto wanted=g_app.settings.organizer_enabled;bool prior=shelf.stopping;auto token=shelf.token;
    scope_exit restore([&]{shelf.lease.reset();shelf.stop.reset();shelf.stopping=prior;shelf.token=token;g_app.settings.organizer_enabled=wanted;});
    g_app.settings.organizer_enabled=1;
    if(!startup_scan_test())return false;
    shelf.lease.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!shelf.lease)return false;
    shelf.token=42;shelf.stopping=true;
    if(!message(WM_APP+91,42,HRESULT_FROM_WIN32(ERROR_BUSY))||!g_app.settings.organizer_enabled) return false;
    shelf.stopping=false;if(!message(WM_APP+91,41,HRESULT_FROM_WIN32(ERROR_BUSY))||!g_app.settings.organizer_enabled) return false;
    shelf.stop.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!shelf.stop) return false;shell_restarted();
    if(!shelf.stopping||!g_app.settings.organizer_enabled||WaitForSingleObject(shelf.stop.get(),0)!=WAIT_OBJECT_0||!shelf.lease)return false;
    // An outstanding fake helper must receive the stop before Windows commits.
    // Its handle stays live while cancellation returns control to the monitor.
    ResetEvent(shelf.stop.get());
    suspend_for_session_end();suspend_for_session_end();
    bool suspended=shelf.session_suspended&&shelf.stopping&&configure()&&!shelf.window&&g_app.settings.organizer_enabled&&WaitForSingleObject(shelf.stop.get(),0)==WAIT_OBJECT_0;
    resume_after_cancelled_session();resume_after_cancelled_session();
    return suspended&&!shelf.session_suspended&&shelf.stopping&&g_app.settings.organizer_enabled;
}
namespace {
bool menu_probe_ok{};int menu_probe_mode{};
BOOL WINAPI test_popup(HMENU menu,UINT,int,int,int,HWND owner,RECT const*) {
    if(menu_probe_mode==2){menu_probe_ok=GetMenuState(menu,21,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,22,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,23,MF_BYCOMMAND)!=UINT(-1);return 0;}
    shelf_proc(shelf.window,WM_TIMER,3,0);shelf_proc(shelf.window,WM_TIMER,2,0);
    menu_probe_ok=shelf.menu&&shelf.model->entries.size()==1&&shelf.model->entries[0].loose&&shelf.loose.size()==1&&IsWindow(owner);
    if(menu_probe_mode==1){DestroyWindow(owner);shelf.loose.clear();++shelf.generation;}
    return 1;
}
}
bool interaction_tests(std::wstring const& directory) {
    if(shelf.window||shelf.lease||shelf.editor||!shelf.loose.empty())return false;
    bool old_fixture=shelf.fixture;shelf.fixture=true;auto old_model=std::move(shelf.model);auto old_roots=shelf.roots;auto old_settings=g_app.settings;auto old_generation=shelf.generation;bool old_stopping=shelf.stopping;auto old_popup=popup_menu;
    HWND window=CreateWindowExW(0,L"STATIC",L"Organizer isolated test",WS_POPUP,0,0,500,300,nullptr,nullptr,g_app.instance,nullptr);if(!window){shelf.fixture=old_fixture;shelf.model=std::move(old_model);return false;}
    scope_exit restore([&]{shelf.fixture=old_fixture;popup_menu=old_popup;for(auto const& item:shelf.loose)if(IsWindow(item->window))DestroyWindow(item->window);shelf.loose.clear();if(shelf.editor)DestroyWindow(shelf.editor);shelf.editor=nullptr;shelf.window=nullptr;shelf.search=nullptr;DestroyWindow(window);shelf.model=std::move(old_model);shelf.roots=std::move(old_roots);shelf.menu=shelf.dragging=shelf.drag_groups=shelf.drag_started=false;shelf.drag_group.clear();shelf.drag_entry.clear();shelf.tiles.clear();shelf.stopping=old_stopping;shelf.generation=old_generation;g_app.settings=old_settings;});
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;shelf.window=window;shelf.stopping=false;shelf.roots.clear();g_app.settings.organizer_auto_collect=1;shelf.model=std::make_unique<Model>(directory);shelf.model->initialize_groups();Entry entry;entry.path=(std::filesystem::path(directory)/L"untouched.txt").wstring();entry.name=L"untouched";entry.group=L"documents";entry.loose=true;entry.due={};shelf.model->entries.push_back(entry);if(!write_utf8_atomic(entry.path.c_str(),L"original")||!shelf.model->save())return false;
    auto loose=std::make_unique<Loose>();loose->path=entry.path;loose->window=CreateWindowExW(0,L"STATIC",L"Hidden loose fixture",WS_POPUP,0,0,80,90,nullptr,nullptr,g_app.instance,nullptr);if(!loose->window)return false;SetWindowLongPtrW(loose->window,GWLP_USERDATA,LPARAM(loose.get()));shelf.loose.push_back(std::move(loose));
    // Both timers must defer while any gesture/editor owns stable Entry identity.
    for(int mode=0;mode<5;++mode){shelf.menu=mode==0;shelf.dragging=mode==1;shelf.loose[0]->dragging=mode==2;shelf.drag_group=mode==3?L"documents":L"";shelf.editor=mode==4?window:nullptr;shelf_proc(window,WM_TIMER,3,0);shelf_proc(window,WM_TIMER,2,0);if(shelf.model->entries.size()!=1||!shelf.model->entries[0].loose||shelf.loose.empty())return false;}
    shelf.menu=shelf.dragging=false;shelf.loose[0]->dragging=false;shelf.drag_group.clear();shelf.editor=nullptr;
    popup_menu=test_popup;menu_probe_mode=0;menu_probe_ok=false;loose_proc(shelf.loose[0]->window,WM_CONTEXTMENU,0,0);if(!menu_probe_ok||shelf.model->entries[0].loose||shelf.menu)return false;
    shelf_proc(window,WM_APP+95,0,0);if(!shelf.loose.empty()||!std::filesystem::exists(entry.path,error))return false;
    shelf.tiles.push_back({RECT{0,0,100,100},0,false,entry.path});menu_probe_mode=2;menu_probe_ok=false;context_menu(POINT{},0);if(!menu_probe_ok)return false;
    Entry shortcut;shortcut.path=(std::filesystem::path(directory)/L"微信.lnk").wstring();
    Entry pc;pc.path=L"shell:MyComputerFolder";pc.system=true;
    shelf.model->entries.push_back(shortcut);shelf.model->entries.push_back(pc);
    Tile painted{RECT{110,0,180,100},1,false,shortcut.path};
    shelf.tiles.push_back(painted);POINT inside{135,50};
    if(hit(inside)<0||!painted.matches_press(shortcut.path,L""))return false;
    std::swap(shelf.model->entries[1],shelf.model->entries[2]);
    if(hit(inside)!=-1||painted.matches_press(pc.path,L""))return false;
    shelf.model->entries.resize(1);
    if(hit(inside)!=-1)return false;
    // Disable/recreate during a nested popup destroys its target; stale command
    // must be ignored even when the model still exists after window destruction.
    shelf.model->entries[0].loose=true;auto transient=std::make_unique<Loose>();transient->path=entry.path;transient->window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,80,90,nullptr,nullptr,g_app.instance,nullptr);if(!transient->window)return false;SetWindowLongPtrW(transient->window,GWLP_USERDATA,LPARAM(transient.get()));HWND target=transient->window;shelf.loose.push_back(std::move(transient));menu_probe_mode=1;menu_probe_ok=false;loose_proc(target,WM_CONTEXTMENU,0,0);if(!menu_probe_ok||!shelf.model->entries[0].loose||!shelf.loose.empty())return false;shelf.menu=false;
    // Idle timer now collects exactly once, without a visible window or desktop lease.
    shelf_proc(window,WM_TIMER,3,0);return !shelf.model->entries[0].loose&&std::filesystem::exists(entry.path,error);
}
}
