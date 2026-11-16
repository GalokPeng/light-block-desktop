#include "organizer.hpp"
#include "organizer_geometry.hpp"
#include "window_shape.hpp"
#include "desktop_surface.hpp"
#include "ui_motion.hpp"
#include "directory_watch.hpp"
#include "icon_cache.hpp"
#include "ui_api.h"
#include <commdlg.h>
#include <set>

namespace lb::organizer {
namespace {
constexpr UINT changed_message=WM_APP+92;
auto popup_menu=&TrackPopupMenu;
struct Tile { RECT bounds;size_t index;bool group; };
struct Loose { HWND window{};std::wstring path;POINT position{},pressed{};bool dragging{},hint{true}; };
struct Shelf {
    std::unique_ptr<Model> model;
    HWND window{},search{},editor{};
    std::vector<Tile> tiles;
    std::vector<std::wstring> roots;
    IconCache icons;
    std::vector<std::unique_ptr<Loose>> loose;
    DirectoryWatch watcher;
    unique_handle lease,stop;
    DWORD token{};
    bool preview{},enabled{},expanded{},moving{},menu{},dragging{},mouse_inside{},stopping{},reduced_motion{};
    std::wstring group,editing_entry,drag_entry,drag_group,drag_origin;
    bool drag_started{},drag_groups{},drag_expanded{};
    uint64_t generation{};
    int hovered{-1},pressed_tile{-1},scroll{},height{},target{};
    POINT pressed{};
    RECT layout_work{};
    ULONGLONG animation{},opacity_tick{};
    lb::motion::Spring height_motion,header_motion,opacity_motion;
    lb::desktop_surface::Buffer surface;
    RECT editor_bounds{};
    HBRUSH edit_brush{};
    HHOOK outside{};UINT dpi{};HFONT font{},small{},heading{};
    struct Hover {float from{},value{},target{};};std::unordered_map<int,Hover> hovers;ULONGLONG hover_started{};
} shelf;
LRESULT CALLBACK shelf_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK loose_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK edit_keys(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void schedule_collect();
void position_edits();
void paint(HWND);
bool interaction_busy() { return shelf.menu||shelf.dragging||!shelf.drag_group.empty()||!shelf.drag_entry.empty()||shelf.editor||std::any_of(shelf.loose.begin(),shelf.loose.end(),[](auto const& item){return item->dragging;}); }
struct MenuGuard {
    bool previous=shelf.menu;uint64_t generation=shelf.generation;
    MenuGuard(){shelf.menu=true;}
    ~MenuGuard(){if(generation==shelf.generation){shelf.menu=previous;schedule_collect();}}
    bool alive()const{return generation==shelf.generation&&shelf.window&&IsWindow(shelf.window)&&shelf.model&&!shelf.stopping;}
};
bool show_groups(wchar_t const* query){return shelf.drag_groups||(g_app.settings.organizer_view==2&&shelf.group.empty()&&!*query);}
int scaled(int value) { return MulDiv(value,int(shelf.dpi?shelf.dpi:GetDpiForSystem()),96); }
ShelfMetrics metrics(){return ShelfMetrics::make(g_app.settings.organizer_icon_size,int(shelf.dpi?shelf.dpi:GetDpiForSystem()));}
void shelf_fonts(){for(auto font:{shelf.font,shelf.small,shelf.heading})if(font)DeleteObject(font);auto create=[](int size,int weight){return CreateFontW(-scaled(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");};shelf.font=create(14,FW_NORMAL);shelf.small=create(12,FW_NORMAL);shelf.heading=create(16,FW_MEDIUM);if(shelf.search)SendMessageW(shelf.search,WM_SETFONT,WPARAM(shelf.font),TRUE);}

COLORREF mixed(COLORREF a,COLORREF b,int weight) { return RGB((GetRValue(a)*weight+GetRValue(b)*(255-weight))/255,(GetGValue(a)*weight+GetGValue(b)*(255-weight))/255,(GetBValue(a)*weight+GetBValue(b)*(255-weight))/255); }
COLORREF foreground(COLORREF color) { return GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114>140000?RGB(24,34,43):RGB(240,245,251); }
std::vector<std::wstring> desktop_roots() {
    std::vector<std::wstring> result;for(auto const& id:{FOLDERID_Desktop,FOLDERID_PublicDesktop}) { PWSTR path=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&path))) { result.emplace_back(path);CoTaskMemFree(path); } }return result;
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
int width() { RECT work=monitor_work();size_t count=shelf.model?(g_app.settings.organizer_view==2?shelf.model->groups.size():shelf.model->view(g_app.settings.organizer_view,L"",L"").size()):10;int cells=std::clamp(int(count)+1,4,11);return std::max(1,std::min(cell_size()*cells+scaled(24),int(work.right-work.left)-scaled(32))); }
void place(int height) {
    if(shelf.dragging&&shelf.moving) return;
    RECT work=monitor_work(),bounds=shelf_bounds(work,width(),height,g_app.settings.organizer_alignment,scaled(22),g_app.settings.organizer_custom_position!=0,g_app.settings.organizer_x,g_app.settings.organizer_y);
    RECT current{};GetWindowRect(shelf.window,&current);bool changed=!EqualRect(&current,&bounds);
    if(changed)SetWindowPos(shelf.window,shelf.preview?HWND_TOP:HWND_BOTTOM,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE|SWP_NOREDRAW);
    shelf.height=bounds.bottom-bounds.top;shelf.layout_work=work;position_edits();
    if(changed)RedrawWindow(shelf.window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    if(!IsWindowVisible(shelf.window))ShowWindow(shelf.window,SW_SHOWNOACTIVATE);
}
LRESULT CALLBACK outside_click(int code,WPARAM wp,LPARAM lp) {
    if(code==HC_ACTION&&(wp==WM_LBUTTONDOWN||wp==WM_RBUTTONDOWN)&&!interaction_busy()&&shelf.expanded) { RECT bounds{};GetWindowRect(shelf.window,&bounds);auto point=reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;if(!PtInRect(&bounds,point)) PostMessageW(shelf.window,WM_APP+94,0,0); }
    return CallNextHookEx(shelf.outside,code,wp,lp);
}
int expanded_height(){RECT work=monitor_work();wchar_t query[260]{};if(shelf.search)GetWindowTextW(shelf.search,query,260);size_t count=show_groups(query)?shelf.model->groups.size():shelf.model->view(g_app.settings.organizer_view,query,shelf.group).size();return metrics().expanded_height(int(count),width(),std::max(base_height(),int(work.bottom-work.top)-scaled(80)),scaled(58));}
void reflow(){if(!shelf.window)return;KillTimer(shelf.window,1);KillTimer(shelf.window,4);shelf.hovered=shelf.pressed_tile=-1;shelf.hovers.clear();shelf.tiles.clear();shelf.scroll=0;shelf.target=shelf.expanded?expanded_height():base_height();shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.expanded?scaled(48):0);place(shelf.target);InvalidateRect(shelf.window,nullptr,FALSE);}
void hover_to(int index){if(index==shelf.hovered)return;shelf.hovered=index;for(auto& pair:shelf.hovers){pair.second.from=pair.second.value;pair.second.target=0;}if(index>=0){auto& value=shelf.hovers[index];value.from=value.value;value.target=1;}shelf.hover_started=GetTickCount64();if(shelf.reduced_motion){for(auto& pair:shelf.hovers)pair.second.value=pair.second.target;InvalidateRect(shelf.window,nullptr,FALSE);}else SetTimer(shelf.window,4,16,nullptr);}
void hover_opacity(bool inside){shelf.mouse_inside=inside;shelf.opacity_motion.aim(std::min(255,g_app.settings.organizer_opacity+(inside?24:0)));shelf.opacity_tick=GetTickCount64();if(shelf.reduced_motion){shelf.opacity_motion.reset(shelf.opacity_motion.target);InvalidateRect(shelf.window,nullptr,FALSE);}else SetTimer(shelf.window,7,16,nullptr);}
void animate(bool expanded) {
    shelf.expanded=expanded;shelf.scroll=0;
    shelf.target=expanded?expanded_height():base_height();hover_to(-1);
    if(!shelf.height_motion.value)shelf.height_motion.reset(shelf.height?shelf.height:base_height());shelf.height_motion.aim(shelf.target);shelf.header_motion.aim(expanded?scaled(48):0);
    shelf.animation=GetTickCount64();if(shelf.reduced_motion){shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.header_motion.target);place(shelf.target);}else SetTimer(shelf.window,1,16,nullptr);
    position_edits();
    ShowWindow(shelf.search,expanded?SW_SHOWNOACTIVATE:SW_HIDE);if(!expanded) { shelf.group.clear();SetWindowTextW(shelf.search,L""); }
    if(expanded&&!shelf.outside) shelf.outside=SetWindowsHookExW(WH_MOUSE_LL,outside_click,g_app.instance,0);
    if(!expanded&&shelf.outside) { UnhookWindowsHookEx(shelf.outside);shelf.outside=nullptr; }
}
void schedule_collect() {
    if(!shelf.window||!shelf.model)return;KillTimer(shelf.window,3);if(!g_app.settings.organizer_auto_collect) return;
    auto now=std::chrono::steady_clock::now();std::chrono::milliseconds delay=std::chrono::minutes(1);bool pending=false;
    for(auto const& entry:shelf.model->entries) if(entry.loose&&!entry.manual_loose) { pending=true;delay=std::min(delay,std::chrono::duration_cast<std::chrono::milliseconds>(entry.due-now)); }
    if(pending) SetTimer(shelf.window,3,UINT(std::clamp<int64_t>(delay.count(),interaction_busy()?200:100,60000)),nullptr);
}
HICON icon_for(Entry const& entry) {
    return shelf.icons.get(entry.path+L"\n"+std::to_wstring(entry.modified),entry.path,entry.directory,entry.system);
}
void draw_entry(HDC dc,Entry const& entry,RECT cell,float hover,COLORREF background,bool pressed=false) {
    auto m=metrics();int icon_size=m.icon,zoom=int(std::lround(icon_size*(.17f*hover-(pressed?.06f:0.f))));
    int tile_side=m.tile,cx=(cell.left+cell.right)/2,top=cell.top+scaled(5);RECT tile{cx-tile_side/2,top,cx+(tile_side+1)/2,top+tile_side};
    auto plate=mixed(g_app.settings.icon_color,background,150);if(hover>0||pressed)plate=mixed(plate,foreground(background),255-int(hover*18)-(pressed?12:0));
    rounded(dc,tile,scaled(18),plate,mixed(plate,background,180));
    HICON icon=icon_for(entry);if(icon){int size=icon_size+zoom;DrawIconEx(dc,cx-size/2,top+(tile_side-size)/2,icon,size,size,0,nullptr,DI_NORMAL);}
    RECT label{cell.left+scaled(2),cell.bottom-m.label,cell.right-scaled(2),cell.bottom-scaled(2)};draw_text(dc,entry.name.c_str(),label,shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

}
void paint(HWND window) {
    PAINTSTRUCT ps;BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);
    if(!shelf.model||!shelf.surface.reserve(client.right,client.bottom)){EndPaint(window,&ps);return;}
    HDC dc=shelf.surface.dc();COLORREF background=g_app.settings.organizer_color;
    unique_gdi<HBRUSH> brush(CreateSolidBrush(background));FillRect(dc,&client,brush.get());
    if(g_app.settings.organizer_glass){COLORREF top=mixed(background,RGB(226,242,247),230),bottom=mixed(background,RGB(18,27,37),235);TRIVERTEX vertices[]={{0,0,COLOR16(GetRValue(top)<<8),COLOR16(GetGValue(top)<<8),COLOR16(GetBValue(top)<<8),0},{client.right,client.bottom,COLOR16(GetRValue(bottom)<<8),COLOR16(GetGValue(bottom)<<8),COLOR16(GetBValue(bottom)<<8),0}};GRADIENT_RECT mesh{0,1};GradientFill(dc,vertices,2,&mesh,1,GRADIENT_FILL_RECT_V);}
    {unique_gdi<HPEN> edge(CreatePen(PS_SOLID,1,mixed(background,RGB(225,242,250),220)));auto pen=SelectObject(dc,edge.get()),fill=SelectObject(dc,GetStockObject(NULL_BRUSH));RoundRect(dc,1,1,client.right-1,client.bottom-1,scaled(36),scaled(36));SelectObject(dc,pen);SelectObject(dc,fill);}shelf.tiles.clear();
    int cell=cell_size(),row=row_size(),columns=metrics().columns(client.right);wchar_t query[260]{};GetWindowTextW(shelf.search,query,260);
    bool groups=show_groups(query);auto entries=shelf.model->view(g_app.settings.organizer_view,query,shelf.group);size_t count=groups?shelf.model->groups.size():entries.size();
    int top=scaled(10)+int(std::lround(shelf.header_motion.value)),limit=shelf.expanded?columns*std::max(1,int(client.bottom-top-scaled(6))/row):std::min(10,columns-1);size_t first=shelf.expanded?size_t(std::max(0,shelf.scroll))*size_t(columns):0;
    for(size_t n=first;n<count&&n<first+size_t(limit);++n) {
        size_t at=n-first;int x=scaled(12)+int(at%size_t(columns))*cell,y=top+int(at/size_t(columns))*row;RECT bounds{x,y,x+cell,y+row};
        auto hover_it=shelf.hovers.find(int(shelf.tiles.size()));float hover=hover_it==shelf.hovers.end()?0.f:hover_it->second.value;
        bool pressed=shelf.pressed_tile==int(shelf.tiles.size())&&shelf.hovered==shelf.pressed_tile;
        if(groups) {
            auto const& group=shelf.model->groups[n];int side=metrics().tile,cx=(bounds.left+bounds.right)/2,top_tile=bounds.top+scaled(5);RECT tile{cx-side/2,top_tile,cx+(side+1)/2,top_tile+side};rounded(dc,tile,scaled(14),mixed(group.color,background,120),mixed(group.color,background,85));
            auto group_entries=shelf.model->view(0,L"",group.id);int gap=scaled(4),mini=(side-scaled(14)-gap)/2,grid=2*mini+gap,origin_x=cx-grid/2,origin_y=top_tile+(side-grid)/2;
            for(size_t k=0;k<std::min<size_t>(4,group_entries.size());++k)if(auto icon=icon_for(shelf.model->entries[group_entries[k]])){int zoom=int(mini*(.1f*hover-(pressed?.06f:0.f)));DrawIconEx(dc,origin_x+int(k%2)*(mini+gap)-zoom/2,origin_y+int(k/2)*(mini+gap)-zoom/2,icon,mini+zoom,mini+zoom,0,nullptr,DI_NORMAL);}
            draw_text(dc,group.name.c_str(),RECT{bounds.left+scaled(2),bounds.bottom-metrics().label,bounds.right-scaled(2),bounds.bottom-scaled(2)},shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);shelf.tiles.push_back({bounds,n,true});
        } else { draw_entry(dc,shelf.model->entries[entries[n]],bounds,hover,background,pressed);shelf.tiles.push_back({bounds,entries[n],false}); }
    }
    if(!shelf.expanded) { RECT search{client.right-cell-scaled(12),scaled(10),client.right-scaled(12),scaled(10)+row};int cx=(search.left+search.right)/2,cy=search.top+scaled(5)+metrics().tile/2,r=scaled(10);unique_gdi<HPEN> pen(CreatePen(PS_SOLID,scaled(2),foreground(background)));auto prior=SelectObject(dc,pen.get()),prior_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,cx-r,cy-r,cx+r,cy+r);MoveToEx(dc,cx+scaled(7),cy+scaled(7),nullptr);LineTo(dc,cx+scaled(15),cy+scaled(15));SelectObject(dc,prior_brush);SelectObject(dc,prior);draw_text(dc,tr(L"搜索",L"Search"),RECT{search.left,search.bottom-metrics().label,search.right,search.bottom-scaled(2)},shelf.small,foreground(background),DT_CENTER|DT_SINGLELINE);shelf.tiles.push_back({search,SIZE_MAX,false}); }
    if(shelf.expanded&&!shelf.group.empty()) { auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& group){return group.id==shelf.group;});if(found!=shelf.model->groups.end()) draw_text(dc,found->name.c_str(),RECT{scaled(24),scaled(12),client.right-scaled(360),scaled(48)},shelf.heading,mixed(foreground(background),background,int(std::clamp(shelf.header_motion.value/std::max(1,scaled(48)),0.,1.)*255)),DT_SINGLELINE|DT_VCENTER); }
    shelf.surface.present(window,client.right,client.bottom,BYTE(std::clamp(shelf.opacity_motion.value,0.,255.)),float(scaled(18)));EndPaint(window,&ps);
}
int hit(POINT point) { for(size_t n=0;n<shelf.tiles.size();++n) if(PtInRect(&shelf.tiles[n].bounds,point)) return int(n);return -1; }
bool model_result(bool ok) { if(!ok)set_status(L"收纳更改未能保存，已恢复原状态；原文件未改动。",L"Organizer changes could not be saved and were reverted. Files are untouched.");return ok; }
void launch(Entry const& entry) { ShellExecuteW(shelf.window,L"open",entry.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);animate(false); }
void rebuild_loose();
void synchronize() {
    if(interaction_busy()){if(shelf.window)SetTimer(shelf.window,2,200,nullptr);return;}
    if(shelf.model && shelf.model->synchronize(shelf.roots,true)) { rebuild_loose();schedule_collect();reflow(); }
}
void rename_group(size_t index) {
    if(index>=shelf.model->groups.size()) return;auto &group=shelf.model->groups[index];shelf.group=group.id;
    if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.editing_entry.clear();shelf.editor_bounds={scaled(22),scaled(10),scaled(282),scaled(42)};shelf.editor=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CLIENTEDGE,L"EDIT",group.name.c_str(),WS_POPUP|ES_AUTOHSCROLL,0,0,scaled(260),scaled(32),shelf.window,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.editor,GWLP_ID,2);lb::window_shape::own_frame(shelf.editor);SendMessageW(shelf.editor,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.editor,EM_SETLIMITTEXT,120,0);SetWindowSubclass(shelf.editor,edit_keys,1,0);position_edits();ShowWindow(shelf.editor,SW_SHOWNOACTIVATE);SetFocus(shelf.editor);SendMessageW(shelf.editor,EM_SETSEL,0,-1);
}
void rename_entry(std::wstring const& path,RECT bounds) {
    auto found=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==path;});if(found==shelf.model->entries.end())return;
    if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.editing_entry=path;RECT client{};GetClientRect(shelf.window,&client);int w=std::min(scaled(260),int(client.right)-scaled(24)),x=std::clamp(int(bounds.left),scaled(12),int(client.right)-w-scaled(12)),y=std::max(scaled(10),int(bounds.bottom)-metrics().label);
    shelf.editor_bounds={x,y,x+w,y+scaled(30)};shelf.editor=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CLIENTEDGE,L"EDIT",found->name.c_str(),WS_POPUP|ES_AUTOHSCROLL,0,0,w,scaled(30),shelf.window,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.editor,GWLP_ID,2);lb::window_shape::own_frame(shelf.editor);SendMessageW(shelf.editor,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.editor,EM_SETLIMITTEXT,240,0);SetWindowSubclass(shelf.editor,edit_keys,1,0);position_edits();ShowWindow(shelf.editor,SW_SHOWNOACTIVATE);SetFocus(shelf.editor);SendMessageW(shelf.editor,EM_SETSEL,0,-1);
}
void commit_title() {
    if(!shelf.editor)return;auto edit=std::exchange(shelf.editor,nullptr);wchar_t title[242]{};GetWindowTextW(edit,title,242);auto path=std::exchange(shelf.editing_entry,{});
    if(shelf.model){if(!path.empty())model_result(shelf.model->rename_entry(path,title));else {auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& group){return group.id==shelf.group;});if(found!=shelf.model->groups.end()){auto before=*found;found->name=title;found->automatic=false;if(!model_result(shelf.model->save()))*found=std::move(before);}}}
    DestroyWindow(edit);if(shelf.window){InvalidateRect(shelf.window,nullptr,FALSE);schedule_collect();}
}
void context_menu(POINT point,int tile) {
    MenuGuard guard;HMENU menu=CreatePopupMenu();scope_exit destroy([&]{DestroyMenu(menu);});std::wstring group_id,path;RECT bounds{};bool system=false,external=false;std::vector<std::wstring> targets;
    // The popup pumps messages. Retain identifiers, never tile indices or Entry
    // references, across that nested loop (refresh/disable can replace either).
    if(tile>=0&&size_t(tile)<shelf.tiles.size()){auto item=shelf.tiles[size_t(tile)];bounds=item.bounds;if(item.group&&item.index<shelf.model->groups.size())group_id=shelf.model->groups[item.index].id;else if(item.index<shelf.model->entries.size()){auto const& entry=shelf.model->entries[item.index];path=entry.path;group_id=entry.group;system=entry.system;external=entry.external;}}
    if(!path.empty()){
        AppendMenuW(menu,MF_STRING,20,tr(L"打开",L"Open"));if(!system)AppendMenuW(menu,MF_STRING,21,tr(L"在文件夹中显示",L"Show in folder"));AppendMenuW(menu,MF_STRING,22,tr(L"更改显示名称…",L"Change display name…"));
        HMENU move=CreatePopupMenu();for(auto const& group:shelf.model->groups)if(group.id!=group_id){targets.push_back(group.id);AppendMenuW(move,MF_STRING,100+targets.size()-1,group.name.empty()?tr(L"未命名分组",L"Untitled group"):group.name.c_str());}if(!targets.empty())AppendMenuW(menu,MF_POPUP,UINT_PTR(move),tr(L"移至分组",L"Move to group"));else DestroyMenu(move);
        AppendMenuW(menu,MF_STRING,23,external&&!system?tr(L"移除快捷入口（保留原文件）",L"Remove shortcut (keep file)"):tr(L"还原到原桌面",L"Restore to desktop"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    }else if(!group_id.empty()) { AppendMenuW(menu,MF_STRING,1,tr(L"重命名分组",L"Rename group"));AppendMenuW(menu,MF_STRING,2,tr(L"分组颜色…",L"Group color…"));AppendMenuW(menu,MF_STRING|(group_id==L"other"?MF_GRAYED:0),3,tr(L"删除分组（保留文件）",L"Remove group (keep files)"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr); }
    AppendMenuW(menu,MF_STRING,10,tr(L"添加新分组",L"New group"));AppendMenuW(menu,MF_STRING,11,tr(L"按名称排列",L"Sort by name"));AppendMenuW(menu,MF_STRING,12,tr(L"按修改时间排列",L"Sort by modified time"));AppendMenuW(menu,MF_STRING,13,tr(L"按分组排列",L"Group view"));AppendMenuW(menu,MF_STRING,14,tr(L"移动位置",L"Move shelf"));AppendMenuW(menu,MF_STRING,15,tr(L"重新同步桌面",L"Refresh desktop"));AppendMenuW(menu,MF_STRING,16,tr(L"新建便签",L"New note"));AppendMenuW(menu,MF_STRING,17,tr(L"收纳设置…",L"Organizer settings…"));AppendMenuW(menu,MF_STRING,18,tr(L"关闭并恢复原生桌面",L"Disable and restore desktop"));
    int id=popup_menu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,shelf.window,nullptr);
    if(!guard.alive())return;
    auto group_index=[&]{auto found=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[&](auto const& g){return g.id==group_id;});return size_t(found-shelf.model->groups.begin());};
    if(!path.empty()&&id>=100&&size_t(id-100)<targets.size()){model_result(shelf.model->collect(path,targets[size_t(id-100)]));reflow();}
    else if(!path.empty()&&id==20){auto found=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==path;});if(found!=shelf.model->entries.end())launch(*found);}
    else if(!path.empty()&&id==21&&!system){PIDLIST_ABSOLUTE item=nullptr;if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr))){SHOpenFolderAndSelectItems(item,0,nullptr,0);CoTaskMemFree(item);}}
    else if(!path.empty()&&id==22)rename_entry(path,bounds);
    else if(!path.empty()&&id==23){model_result(shelf.model->restore_entry(path));PostMessageW(shelf.window,WM_APP+95,0,0);}
    else if(id>=1&&id<=3&&group_index()<shelf.model->groups.size()) {
        if(id==1){shelf.group=group_id;animate(true);rename_group(group_index());}
        else if(id==2){static COLORREF colors[16];CHOOSECOLORW choose{sizeof choose};choose.hwndOwner=shelf.window;choose.rgbResult=shelf.model->groups[group_index()].color;choose.lpCustColors=colors;choose.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&choose)&&guard.alive()&&group_index()<shelf.model->groups.size()){auto& group=shelf.model->groups[group_index()];auto prior=group.color;group.color=choose.rgbResult;if(!model_result(shelf.model->save()))group.color=prior;}}
        else {if(model_result(shelf.model->remove_group(group_id)))animate(false);}
    }
    else if(id==10) {auto before=*shelf.model;shelf.group=shelf.model->create_group(tr(L"新分组",L"New group")).id;if(model_result(shelf.model->save())){animate(true);rename_group(shelf.model->groups.size()-1);}else {*shelf.model=std::move(before);shelf.group.clear();}}
    else if(id>=11&&id<=13) { g_app.settings.organizer_view=id-11;g_app.settings.organizer_views[std::clamp(g_app.settings.organizer_alignment,0,2)]=id-11;save_config();animate(false); }
    else if(id==14) { shelf.moving=true;set_status(L"可拖动收纳背景移动位置，按 Esc 结束",L"Drag the shelf background to move it. Escape finishes."); }
    else if(id==15) synchronize();else if(id==16) note_create(shelf.window);else if(id==17) settings_show(1);
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
    if(shelf.search){SetWindowPos(shelf.search,nullptr,bounds.right-scaled(24)-edit_width,bounds.top+scaled(13),edit_width,scaled(32),SWP_NOACTIVATE|SWP_NOZORDER);SetLayeredWindowAttributes(shelf.search,0,BYTE(std::clamp(shelf.header_motion.value/std::max(1,scaled(48))*255,1.,255.)),LWA_ALPHA);}
    if(shelf.editor){auto r=shelf.editor_bounds;SetWindowPos(shelf.editor,nullptr,bounds.left+r.left,bounds.top+r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_NOZORDER);}
}
LRESULT CALLBACK edit_keys(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    if(message==WM_NCPAINT){HDC dc=GetWindowDC(window);RECT rect{};GetWindowRect(window,&rect);OffsetRect(&rect,-rect.left,-rect.top);unique_gdi<HBRUSH> edge(CreateSolidBrush(mixed(g_app.settings.organizer_color,foreground(g_app.settings.organizer_color),190)));FrameRect(dc,&rect,edge.get());InflateRect(&rect,-1,-1);unique_gdi<HBRUSH> inside(CreateSolidBrush(g_app.settings.organizer_color));FrameRect(dc,&rect,inside.get());ReleaseDC(window,dc);return 0;}
    if(message==WM_MOUSEMOVE&&!shelf.mouse_inside)hover_opacity(true);
    (void)data;if(message==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)) { if(id==1) { if(wp==VK_RETURN) commit_title();else { auto edit=std::exchange(shelf.editor,nullptr);shelf.editing_entry.clear();DestroyWindow(edit);schedule_collect(); } }else if(wp==VK_ESCAPE) animate(false);return 0; }
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,edit_keys,id);
    if(message==WM_KILLFOCUS&&id==1&&shelf.editor==window&&shelf.window)PostMessageW(shelf.window,WM_COMMAND,MAKEWPARAM(2,EN_KILLFOCUS),LPARAM(window));
    auto result=DefSubclassProc(window,message,wp,lp);
    if(id==2&&shelf.window&&(message==WM_CHAR||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==WM_IME_COMPOSITION))PostMessageW(shelf.window,WM_COMMAND,MAKEWPARAM(1,EN_CHANGE),LPARAM(window));
    return result;
}
void rebuild_loose() {
    if(interaction_busy()){if(shelf.window)SetTimer(shelf.window,5,200,nullptr);return;}
    for(auto it=shelf.loose.begin();it!=shelf.loose.end();) { bool found=std::any_of(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& entry){return entry.loose&&entry.path==(*it)->path;});if(!found) { if((*it)->window) DestroyWindow((*it)->window);it=shelf.loose.erase(it); }else ++it; }
    RECT work=monitor_work();int loose_width=std::max(scaled(104),cell_size()),loose_height=row_size()+scaled(22);for(auto const& item:shelf.loose)if(item->window)SetWindowPos(item->window,nullptr,0,0,loose_width,loose_height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);size_t count=shelf.loose.size();
    for(auto const& entry:shelf.model->entries) if(entry.loose&&!std::any_of(shelf.loose.begin(),shelf.loose.end(),[&](auto const& item){return item->path==entry.path;})) {
        auto item=std::make_unique<Loose>();item->path=entry.path;int rows=std::max(1,int(work.bottom-work.top)/scaled(112));item->position={work.left+scaled(18)+int(count/size_t(rows))*scaled(104),work.top+scaled(12)+int(count%size_t(rows))*scaled(112)};if(entry.positioned){MONITORINFO info{sizeof info};if(GetMonitorInfoW(MonitorFromPoint(entry.position,MONITOR_DEFAULTTONEAREST),&info))item->position={std::clamp(entry.position.x,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-loose_width)),std::clamp(entry.position.y,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-loose_height))};}++count;
        item->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LightBlockDesktopNative.LooseIcon",L"",WS_POPUP,item->position.x,item->position.y,loose_width,loose_height,GetShellWindow(),nullptr,g_app.instance,item.get());if(item->window) { SetLayeredWindowAttributes(item->window,0,245,LWA_ALPHA);SetWindowPos(item->window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);SetTimer(item->window,1,10000,nullptr);shelf.loose.push_back(std::move(item)); }
    }
}
LRESULT CALLBACK loose_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto item=reinterpret_cast<Loose*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE) { item=static_cast<Loose*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(item)); }
    if(!item) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: { PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT bounds{};GetClientRect(window,&bounds);unique_gdi<HBRUSH> brush(CreateSolidBrush(g_app.settings.organizer_color));FillRect(dc,&bounds,brush.get());auto entry=std::find_if(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& e){return e.path==item->path;});if(entry!=shelf.model->entries.end()) { RECT cell{0,0,bounds.right,row_size()};draw_entry(dc,*entry,cell,false,g_app.settings.organizer_color); }if(item->hint) draw_text(dc,tr(L"右键放入收纳",L"Right-click to collect"),RECT{0,row_size(),bounds.right,bounds.bottom},shelf.small,RGB(183,226,213),DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);EndPaint(window,&ps);return 0; }
    case WM_TIMER:KillTimer(window,1);item->hint=false;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONDBLCLK:ShellExecuteW(window,L"open",item->path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;
    case WM_LBUTTONDOWN:GetCursorPos(&item->pressed);item->dragging=true;SetCapture(window);return 0;
    case WM_MOUSEMOVE:if(item->dragging) { POINT point;GetCursorPos(&point);RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,rect.left+point.x-item->pressed.x,rect.top+point.y-item->pressed.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);item->pressed=point; }return 0;
    case WM_LBUTTONUP:{auto path=item->path;item->dragging=false;RECT rect{};GetWindowRect(window,&rect);ReleaseCapture();POINT point{};GetCursorPos(&point);ScreenToClient(shelf.window,&point);int target=hit(point);if(target>=0&&shelf.tiles[size_t(target)].group){model_result(shelf.model->collect(path,shelf.model->groups[shelf.tiles[size_t(target)].index].id));PostMessageW(shelf.window,WM_APP+95,0,0);}else model_result(shelf.model->position_entry(path,POINT{rect.left,rect.top}));schedule_collect();return 0;}
    case WM_CAPTURECHANGED:item->dragging=false;schedule_collect();return 0;
    case WM_CONTEXTMENU: { MenuGuard guard;auto path=item->path;std::vector<std::wstring> targets;HMENU menu=CreatePopupMenu();scope_exit destroy([&]{DestroyMenu(menu);});AppendMenuW(menu,MF_STRING,1,tr(L"放入收纳",L"Collect"));for(auto const& group:shelf.model->groups) { targets.push_back(group.id);auto label=std::wstring(tr(L"放入分组：",L"Move to: "))+(group.name.empty()?tr(L"未命名分组",L"Untitled group"):group.name.c_str());AppendMenuW(menu,MF_STRING,99+targets.size(),label.c_str()); }POINT point;GetCursorPos(&point);int selected=popup_menu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);if(!guard.alive()||!IsWindow(window)||GetWindowLongPtrW(window,GWLP_USERDATA)!=LPARAM(item))return 0;if(selected==1||(selected>=100&&size_t(selected-100)<targets.size())) { auto group=selected>=100?targets[size_t(selected-100)]:L"";model_result(shelf.model->collect(path,group));PostMessageW(shelf.window,WM_APP+95,0,0); }return 0; }
    case WM_DESTROY:KillTimer(window,1);return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(window,GWLP_USERDATA,0);return DefWindowProcW(window,message,wp,lp);
    }return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK shelf_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE:shelf.window=window;DragAcceptFiles(window,TRUE);shelf.search=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_CLIENTEDGE,L"EDIT",L"",WS_POPUP|ES_AUTOHSCROLL|WS_TABSTOP,0,0,scaled(360),scaled(32),window,nullptr,g_app.instance,nullptr);SetWindowLongPtrW(shelf.search,GWLP_ID,1);lb::window_shape::own_frame(shelf.search);SendMessageW(shelf.search,WM_SETFONT,WPARAM(shelf.font),TRUE);SendMessageW(shelf.search,EM_SETCUEBANNER,TRUE,LPARAM(tr(L"搜索桌面与分组",L"Search desktop and groups")));SetWindowSubclass(shelf.search,edit_keys,2,0);shelf.icons.start(window,WM_APP+93);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint(window);return 0;
    case WM_SIZE:position_edits();InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_WINDOWPOSCHANGED:position_edits();break;
    case WM_CTLCOLOREDIT:SetBkColor(HDC(wp),g_app.settings.organizer_color);SetTextColor(HDC(wp),foreground(g_app.settings.organizer_color));return LRESULT(shelf.edit_brush);
    case WM_MOUSEMOVE: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(shelf.dragging&&shelf.moving) { POINT cursor;GetCursorPos(&cursor);RECT rect{};GetWindowRect(window,&rect);SetWindowPos(window,nullptr,rect.left+cursor.x-shelf.pressed.x,rect.top+cursor.y-shelf.pressed.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);shelf.pressed=cursor; }
        else if((!shelf.drag_entry.empty()||!shelf.drag_group.empty())&&!shelf.drag_started&&(std::abs(point.x-shelf.pressed.x)>GetSystemMetrics(SM_CXDRAG)||std::abs(point.y-shelf.pressed.y)>GetSystemMetrics(SM_CYDRAG))){shelf.drag_started=true;if(!shelf.drag_entry.empty()){shelf.drag_groups=true;shelf.expanded=true;shelf.group.clear();ShowWindow(shelf.search,SW_HIDE);reflow();UpdateWindow(window);GetCursorPos(&point);ScreenToClient(window,&point);}}
        hover_to(hit(point));TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,window,0};TrackMouseEvent(&track);if(!shelf.mouse_inside)hover_opacity(true);return 0; }
    case WM_MOUSELEAVE:hover_opacity(false);hover_to(-1);InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONDOWN: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int target=hit(point);shelf.pressed_tile=target;InvalidateRect(window,nullptr,FALSE);if(shelf.editor) commit_title();shelf.drag_group.clear();shelf.drag_entry.clear();shelf.drag_started=false;shelf.pressed=point;shelf.drag_origin=shelf.group;shelf.drag_expanded=shelf.expanded;
        if(shelf.moving){shelf.dragging=true;GetCursorPos(&shelf.pressed);SetCapture(window);}
        else if(target>=0){auto tile=shelf.tiles[size_t(target)];if(tile.group&&tile.index<shelf.model->groups.size())shelf.drag_group=shelf.model->groups[tile.index].id;else if(tile.index<shelf.model->entries.size())shelf.drag_entry=shelf.model->entries[tile.index].path;SetCapture(window);}return 0; }
    case WM_LBUTTONUP: {
        if(shelf.dragging) { shelf.dragging=false;ReleaseCapture();RECT rect{};GetWindowRect(window,&rect);g_app.settings.organizer_custom_position=1;g_app.settings.organizer_x=rect.left;g_app.settings.organizer_y=rect.top;place(shelf.height);GetWindowRect(window,&rect);g_app.settings.organizer_x=rect.left;g_app.settings.organizer_y=rect.top;save_config();return 0; }
        int target=hit(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});int pressed=std::exchange(shelf.pressed_tile,-1);InvalidateRect(window,nullptr,FALSE);auto source_group=std::exchange(shelf.drag_group,{}),source_entry=std::exchange(shelf.drag_entry,{});bool dragged=std::exchange(shelf.drag_started,false),groups=std::exchange(shelf.drag_groups,false);if(GetCapture()==window)ReleaseCapture();
        if(dragged){if(target>=0&&shelf.tiles[size_t(target)].group){auto destination=shelf.tiles[size_t(target)].index;if(!source_entry.empty())model_result(shelf.model->collect(source_entry,shelf.model->groups[destination].id));else if(!source_group.empty()){auto from=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[&](auto const& group){return group.id==source_group;});if(from!=shelf.model->groups.end())model_result(shelf.model->move_group(size_t(from-shelf.model->groups.begin()),destination));}}
            if(groups){shelf.group=shelf.drag_origin;shelf.expanded=shelf.drag_expanded;ShowWindow(shelf.search,shelf.expanded?SW_SHOW:SW_HIDE);}reflow();schedule_collect();return 0;}
        if(target>=0&&target==pressed) { auto const tile=shelf.tiles[size_t(target)];if(tile.index==SIZE_MAX) { shelf.group.clear();animate(true);SetForegroundWindow(window);SetFocus(shelf.search); }else if(tile.group) { shelf.group=shelf.model->groups[tile.index].id;animate(true); }else { launch(shelf.model->entries[tile.index]); } }
        else if(shelf.expanded) { if(!shelf.group.empty()&&GET_Y_LPARAM(lp)<scaled(55)) { auto it=std::find_if(shelf.model->groups.begin(),shelf.model->groups.end(),[](auto const& g){return g.id==shelf.group;});if(it!=shelf.model->groups.end()) { rename_group(size_t(it-shelf.model->groups.begin()));SetWindowSubclass(shelf.editor,edit_keys,1,0); } }else animate(false); }return 0;
    }
    case WM_CAPTURECHANGED:{shelf.pressed_tile=-1;InvalidateRect(window,nullptr,FALSE);bool changed=shelf.drag_groups;shelf.dragging=false;shelf.drag_group.clear();shelf.drag_entry.clear();shelf.drag_started=false;shelf.drag_groups=false;if(changed){shelf.group=shelf.drag_origin;shelf.expanded=shelf.drag_expanded;ShowWindow(shelf.search,shelf.expanded?SW_SHOW:SW_HIDE);reflow();}schedule_collect();return 0;}
    case WM_CONTEXTMENU: { POINT point;GetCursorPos(&point);POINT local=point;ScreenToClient(window,&local);context_menu(point,hit(local));return 0; }
    case WM_DROPFILES:{HDROP drop=reinterpret_cast<HDROP>(wp);scope_exit finish([&]{DragFinish(drop);});POINT point{};DragQueryPoint(drop,&point);std::wstring group=shelf.group;int target=hit(point);if(target>=0&&shelf.tiles[size_t(target)].group)group=shelf.model->groups[shelf.tiles[size_t(target)].index].id;if(group.empty())group=L"other";UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count>10000)return 0;std::vector<std::wstring> paths;for(UINT n=0;n<count;++n){UINT length=DragQueryFileW(drop,n,nullptr,0);if(!length||length>=LB_PATH)return 0;std::wstring path(length+1,L'\0');DragQueryFileW(drop,n,path.data(),length+1);path.resize(length);paths.push_back(std::move(path));}if(model_result(shelf.model->add_paths(paths,group))){rebuild_loose();schedule_collect();reflow();}return 0;}
    case WM_COMMAND:if(LOWORD(wp)==1&&HIWORD(wp)==EN_CHANGE&&HWND(lp)==shelf.search) { shelf.scroll=0;if(shelf.expanded)reflow();InvalidateRect(window,nullptr,FALSE); }else if(LOWORD(wp)==2&&HIWORD(wp)==EN_KILLFOCUS&&HWND(lp)==shelf.editor) commit_title();return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE) { if(GetCapture()==window){ReleaseCapture();return 0;}shelf.moving=false;animate(false); }return 0;
    case WM_MOUSEWHEEL:if(shelf.expanded) { int columns=std::max(1,(width()-scaled(24))/cell_size());wchar_t query[260]{};GetWindowTextW(shelf.search,query,260);size_t count=g_app.settings.organizer_view==2&&shelf.group.empty()&&!*query?shelf.model->groups.size():shelf.model->view(g_app.settings.organizer_view,query,shelf.group).size();int max_rows=std::max(0,int((count+size_t(columns)-1)/size_t(columns))-std::max(1,(shelf.height-scaled(58)-scaled(6))/row_size()));shelf.scroll=std::clamp(shelf.scroll+(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1),0,max_rows);InvalidateRect(window,nullptr,FALSE); }return 0;
    case WM_TIMER:
        if(wp==1) { auto now=GetTickCount64();double seconds=double(now-shelf.animation)/1000.;bool active=shelf.height_motion.step(seconds,shelf.reduced_motion);active=shelf.header_motion.step(seconds,shelf.reduced_motion)||active;shelf.animation=now;place(int(std::lround(shelf.height_motion.value)));InvalidateRect(window,nullptr,FALSE);if(!active)KillTimer(window,1); }
        else if(wp==7){auto now=GetTickCount64();bool active=shelf.opacity_motion.step(double(now-shelf.opacity_tick)/1000.,shelf.reduced_motion);shelf.opacity_tick=now;InvalidateRect(window,nullptr,FALSE);if(!active)KillTimer(window,7);}
        else if(wp==4){float t=std::min(1.f,float(GetTickCount64()-shelf.hover_started)/135.f),ease=1-std::pow(1-t,3);for(auto it=shelf.hovers.begin();it!=shelf.hovers.end();){auto& value=it->second;value.value=value.from+(value.target-value.from)*ease;if(t>=1&&value.target==0)it=shelf.hovers.erase(it);else ++it;}InvalidateRect(window,nullptr,FALSE);if(t>=1)KillTimer(window,4);}
        else if(wp==2) { KillTimer(window,2);synchronize(); }
        else if(wp==3) { KillTimer(window,3);if(interaction_busy()){SetTimer(window,3,200,nullptr);return 0;}if(g_app.settings.organizer_auto_collect){auto now=std::chrono::steady_clock::now();bool due=std::any_of(shelf.model->entries.begin(),shelf.model->entries.end(),[&](auto const& entry){return entry.loose&&!entry.manual_loose&&entry.due<=now;});if(shelf.model->collect_due(now)){rebuild_loose();InvalidateRect(window,nullptr,FALSE);}else if(due){SetTimer(window,3,2000,nullptr);return 0;}}schedule_collect(); }
        else if(wp==5){KillTimer(window,5);if(interaction_busy())SetTimer(window,5,200,nullptr);else {rebuild_loose();schedule_collect();reflow();}}
        else if(wp==6&&!interaction_busy()){auto work=monitor_work();if(!EqualRect(&work,&shelf.layout_work))reflow();}return 0;
    case changed_message:if(wp==shelf.token) SetTimer(window,2,250,nullptr);return 0;
    case WM_APP+93:shelf.icons.receive();InvalidateRect(window,nullptr,FALSE);for(auto const& item:shelf.loose) InvalidateRect(item->window,nullptr,FALSE);return 0;
    case WM_APP+94:if(!interaction_busy()) { commit_title();animate(false); }return 0;
    case WM_APP+95:rebuild_loose();schedule_collect();InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_DPICHANGED:shelf.dpi=HIWORD(wp);shelf_fonts();reflow();return 0;
    case WM_DISPLAYCHANGE:reflow();return 0;
    case WM_SETTINGCHANGE:if(wp==SPI_SETWORKAREA)reflow();else{appearance(window,scaled(36));if(shelf.reduced_motion){KillTimer(window,1);KillTimer(window,4);KillTimer(window,7);shelf.height_motion.reset(shelf.target);shelf.header_motion.reset(shelf.expanded?scaled(48):0);place(shelf.target);}}return 0;
    case WM_CLOSE:if(shelf.preview) shutdown();return 0;
    case WM_DESTROY:for(auto timer:{1,2,3,4,5,6,7})KillTimer(window,timer);if(shelf.search)DestroyWindow(std::exchange(shelf.search,nullptr));if(shelf.editor)DestroyWindow(std::exchange(shelf.editor,nullptr));shelf.surface.clear();if(shelf.edit_brush)DeleteObject(std::exchange(shelf.edit_brush,nullptr));shelf.icons.stop();if(shelf.outside) UnhookWindowsHookEx(std::exchange(shelf.outside,nullptr));shelf.window=nullptr;shelf.search=nullptr;shelf.editor=nullptr;shelf.editing_entry.clear();shelf.drag_entry.clear();shelf.drag_group.clear();shelf.drag_groups=shelf.drag_started=shelf.dragging=shelf.menu=false;for(auto font:{shelf.font,shelf.small,shelf.heading})if(font)DeleteObject(font);shelf.font=shelf.small=shelf.heading=nullptr;shelf.hovers.clear();return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
bool show_shelf() {
    ++shelf.generation;
    shelf.model=std::make_unique<Model>(g_app.data);if(!shelf.model->load()) { error_box(g_app.window,tr(L"收纳配置损坏，未修改原文件",L"Organizer configuration is invalid. Existing files were preserved."),E_INVALIDARG);return false; }
    if(shelf.preview){auto root=std::filesystem::path(g_app.data)/L"sample-desktop";std::error_code error;for(auto name:{L"Documents",L"Pictures",L"Projects",L"Music",L"Videos",L"Notes",L"Archive"}){std::filesystem::create_directories(root/name,error);if(error)return false;}shelf.roots={root.wstring()};}else shelf.roots=desktop_roots();
    if(!shelf.model->synchronize(shelf.roots,false)) return false;shelf.dpi=GetDpiForSystem();shelf_fonts();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpfnWndProc=shelf_proc;cls.lpszClassName=L"LightBlockDesktopNative.Organizer";RegisterClassW(&cls);cls.style=CS_DBLCLKS;cls.lpfnWndProc=loose_proc;cls.lpszClassName=L"LightBlockDesktopNative.LooseIcon";RegisterClassW(&cls);
    HWND window=CreateWindowExW((shelf.preview?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_LAYERED,L"LightBlockDesktopNative.Organizer",tr(L"轻块桌面 · 收纳",L"Light Block Desktop · Organizer"),WS_POPUP,0,0,scaled(1050),base_height(),shelf.preview?nullptr:GetShellWindow(),nullptr,g_app.instance,nullptr);
    if(!window) return false;shelf.enabled=true;shelf.expanded=false;shelf.mouse_inside=false;shelf.header_motion.reset(0);shelf.height_motion.reset(base_height());shelf.group.clear();appearance(window,scaled(36));place(base_height());SetTimer(window,6,750,nullptr);rebuild_loose();schedule_collect();shelf.watcher.start(shelf.roots,window,changed_message,shelf.token);return true;
}
bool start_lease(bool restore_only) {
    auto stop_name=L"Local\\LightBlockNative.IconStop."+std::to_wstring(GetCurrentProcessId());shelf.stop.reset(CreateEventW(nullptr,TRUE,FALSE,stop_name.c_str()));if(!shelf.stop) return false;ResetEvent(shelf.stop.get());
    FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return false;ULONGLONG timestamp=(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    wchar_t exe[LB_PATH*2],data[LB_PATH*2];if(!quote_arg(exe,ARRAYSIZE(exe),g_app.exe)||!quote_arg(data,ARRAYSIZE(data),g_app.data)) return false;
    auto command=std::wstring(exe)+L" --icon-lease --owner "+std::to_wstring(uintptr_t(g_app.window))+L" --token "+std::to_wstring(++shelf.token)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --parent-created "+std::to_wstring(timestamp)+L" --data-dir "+data+(restore_only?L" --restore-only":L"");
    STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION process{};if(!CreateProcessW(g_app.exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,g_app.directory,&startup,&process)) return false;CloseHandle(process.hThread);shelf.lease.reset(process.hProcess);SetTimer(g_app.window,17,100,nullptr);return true;
}
}
bool configure(bool preview) {
    preview=preview||shelf.preview;
    if(preview&&!g_app.settings.organizer_enabled){shelf.preview=true;return shutdown();}
    if(shelf.lease&&WaitForSingleObject(shelf.lease.get(),0)!=WAIT_OBJECT_0) { if(!g_app.settings.organizer_enabled&&!shelf.preview) shutdown();else if(shelf.window) { reflow();rebuild_loose();appearance(shelf.window,scaled(26));schedule_collect();InvalidateRect(shelf.window,nullptr,FALSE); }return true; }
    shelf.stopping=false;
    if(shelf.window) { if(!g_app.settings.organizer_enabled&&!preview) return shutdown();reflow();rebuild_loose();appearance(shelf.window,scaled(26));InvalidateRect(shelf.window,nullptr,FALSE);return true; }
    shelf.preview=preview;if(preview) return show_shelf();
    if(g_app.settings.organizer_enabled) return start_lease(false);
    std::error_code error;if(std::filesystem::exists(std::filesystem::path(g_app.data)/L"native-desktop-icons.json",error)) return start_lease(true);return true;
}
bool has_session() { return bool(shelf.lease); }
bool shutdown() {
    ++shelf.generation;
    shelf.stopping=true;shelf.enabled=false;commit_title();shelf.watcher.stop();for(auto &item:shelf.loose) if(item->window) DestroyWindow(item->window);shelf.loose.clear();if(shelf.window) DestroyWindow(shelf.window);shelf.window=nullptr;shelf.search=nullptr;shelf.editor=nullptr;if(shelf.stop) SetEvent(shelf.stop.get());
    if(shelf.lease&&WaitForSingleObject(shelf.lease.get(),0)!=WAIT_OBJECT_0) { if(g_app.window) SetTimer(g_app.window,17,100,nullptr);return false; }shelf.lease.reset();shelf.stop.reset();return true;
}
void tick(UINT timer) {
    if(timer!=17) return;if(!shelf.lease) { KillTimer(g_app.window,17);return; }
    if(WaitForSingleObject(shelf.lease.get(),0)==WAIT_OBJECT_0) {
        DWORD code=0;GetExitCodeProcess(shelf.lease.get(),&code);shelf.lease.reset();shelf.stop.reset();KillTimer(g_app.window,17);
        bool wanted=g_app.settings.organizer_enabled&&!g_app.close_requested,restart=shelf.stopping&&wanted,failed=!shelf.stopping&&wanted;shelf.stopping=false;
        if(restart) { if(!configure()) { g_app.settings.organizer_enabled=0;save_config();set_status(L"桌面收纳未能重新启动，原桌面已保留",L"Organizer could not restart. Your original desktop was retained."); } }
        else if(failed) { shutdown();g_app.settings.organizer_enabled=0;save_config();set_status(code?L"桌面控制已退出，原桌面已恢复或等待恢复":L"桌面收纳会话已结束，原桌面已恢复",code?L"Desktop ownership ended. The original desktop was restored or recovery remains pending.":L"Organizer session ended. Your original desktop was restored."); }
    }
}
void desktop_changed() { if(shelf.window) SetTimer(shelf.window,2,250,nullptr); }
void shell_restarted() {
    if(shelf.preview||!g_app.settings.organizer_enabled||g_app.close_requested) return;
    // Retire the old IFolderView lease before claiming the replacement Explorer
    // desktop. Its durable receipt keeps the user's original icon visibility.
    // tick() restarts after the old helper has acknowledged restoration.
    if(shutdown()) {shelf.stopping=false;if(!configure()) {g_app.settings.organizer_enabled=0;save_config();set_status(L"Explorer 桌面暂不可用，可稍后重新开启收纳",L"The Explorer desktop is unavailable. Re-enable the organizer after it recovers.");}}
}
namespace {
std::unique_ptr<Model> settings_model;
Model* editable_model(bool reload) {
    if(shelf.window&&shelf.model)return shelf.model.get();
    if(reload||!settings_model){auto next=std::make_unique<Model>(g_app.data);if(!next->load())return nullptr;settings_model=std::move(next);}return settings_model.get();
}
}
int settings_group(int index,UiGroup* out) {
    if(index<0||!out)return 0;auto model=editable_model(index==0);if(!model||size_t(index)>=model->groups.size())return 0;auto const& group=model->groups[size_t(index)];*out={};StringCchCopyW(out->id,104,group.id.c_str());StringCchCopyW(out->name,128,group.name.c_str());out->color=int(group.color);out->count=int(std::count_if(model->entries.begin(),model->entries.end(),[&](auto const& e){return e.group==group.id;}));return 1;
}
int settings_group_action(int action,wchar_t const* identifier,wchar_t const* argument,int value) {
    auto model=editable_model(true);if(!model)return 0;std::wstring id=identifier?identifier:L"",arg=argument?argument:L"";if(id.size()>100||arg.size()>=LB_PATH)return 0;auto before=*model;bool ok=false;
    auto found=std::find_if(model->groups.begin(),model->groups.end(),[&](auto const& group){return group.id==id;});
    if(action==0&&arg.size()<=120&&model->groups.size()<128){model->create_group(arg);ok=true;}
    else if(action==4){ok=model->synchronize(desktop_roots(),false);}
    else if(found!=model->groups.end()) {
        if(action==1&&arg.size()<=120){found->name=arg;found->automatic=false;ok=true;}
        else if(action==2&&value>=0&&value<=0xffffff){found->color=COLORREF(value);ok=true;}
        else if(action==3&&id!=L"other")ok=model->remove_group(id);
        else if(action==5||action==6){
            bool system=action==6;DWORD attributes=system?0:GetFileAttributesW(arg.c_str());if((system&&(arg==L"shell:MyComputerFolder"||arg==L"shell:RecycleBinFolder"||arg==L"ms-settings:"))||(!system&&attributes!=INVALID_FILE_ATTRIBUTES&&!PathIsRelativeW(arg.c_str()))) {
                auto entry=std::find_if(model->entries.begin(),model->entries.end(),[&](auto const& e){return !_wcsicmp(e.path.c_str(),arg.c_str());});
                if(entry!=model->entries.end()){entry->group=id;entry->loose=false;entry->manual_loose=false;ok=true;}else if(model->entries.size()<10000){Entry item;item.path=arg;item.name=system?(arg==L"shell:MyComputerFolder"?tr(L"此电脑",L"This PC"):arg==L"shell:RecycleBinFolder"?tr(L"回收站",L"Recycle Bin"):tr(L"Windows 设置",L"Windows settings")):PathFindFileNameW(arg.c_str());item.group=id;item.system=system;item.external=true;item.directory=!system&&(attributes&FILE_ATTRIBUTE_DIRECTORY);model->entries.push_back(std::move(item));ok=true;}
            }
        }
    }
    if(ok&&action!=3&&action!=4)ok=model->save();if(!ok){*model=std::move(before);set_status(L"分组更改未保存，请重试；原文件保持不变。",L"Group change could not be saved. Files remain unchanged.");return 0;}
    if(shelf.window){if(!shelf.group.empty()&&!std::any_of(model->groups.begin(),model->groups.end(),[](auto const& g){return g.id==shelf.group;}))shelf.group.clear();rebuild_loose();schedule_collect();InvalidateRect(shelf.window,nullptr,FALSE);}return 1;
}
bool message(UINT msg,WPARAM wp,LPARAM lp) {
    if(msg!=WM_APP+90&&msg!=WM_APP+91) return false;if(wp!=shelf.token||!shelf.lease) return true;
    if(msg==WM_APP+90) { if(g_app.settings.organizer_enabled&&!g_app.close_requested&&!shelf.stopping) { if(!shelf.window&&!show_shelf()&&shelf.stop) SetEvent(shelf.stop.get()); }else if(shelf.stop) SetEvent(shelf.stop.get()); }
    else {
        if(shelf.stop) SetEvent(shelf.stop.get());
        // This helper may belong to an earlier off -> on transition. Its result
        // must never replace the user's latest desired state.
        if(shelf.stopping||g_app.close_requested) return true;
        g_app.settings.organizer_enabled=0;save_config();if(HRESULT(lp)==HRESULT_FROM_WIN32(ERROR_BUSY)) set_status(L"其他程序正在管理桌面，请先关闭其收纳功能。",L"Another app owns the desktop. Disable its organizer first.");else error_box(g_app.window,tr(L"无法接管桌面，原桌面保持不变",L"Could not claim the desktop. The original desktop is preserved."),HRESULT(lp));
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
bool surface_tests(std::wstring const& directory){
    if(shelf.window||shelf.lease||shelf.model)return false;
    auto previous=g_app.settings;auto dpi=shelf.dpi;
    scope_exit restore([&]{if(shelf.window)DestroyWindow(shelf.window);shelf.model.reset();shelf.tiles.clear();shelf.dpi=dpi;g_app.settings=previous;});
    shelf.model=std::make_unique<Model>(directory);shelf.model->initialize_groups();shelf.dpi=96;shelf_fonts();
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=shelf_proc;cls.lpszClassName=L"LightBlockDesktopNative.OrganizerSurfaceTest";RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,cls.lpszClassName,L"Isolated alpha surface",WS_POPUP,-32000,-32000,500,140,nullptr,nullptr,g_app.instance,nullptr);if(!window)return false;
    g_app.settings.organizer_color=RGB(40,90,120);g_app.settings.organizer_opacity=210;appearance(window,36);
    for(int size:{0,1,2})for(int align:{0,1,2}){
        g_app.settings.organizer_icon_size=size;g_app.settings.organizer_alignment=align;
        int height=base_height(),width=600;SetWindowPos(window,nullptr,-32000,-32000,width,height,SWP_NOACTIVATE|SWP_NOZORDER);paint(window);
        if(IsWindowVisible(window)||IsWindowVisible(shelf.search)||!shelf.surface.pixels())return false;
        auto p=shelf.surface.pixels();int stride=shelf.surface.stride();if(p[0]||p[width-1]||p[size_t(height-1)*stride]||p[size_t(height-1)*stride+width-1])return false;
        if((p[size_t(height/2)*stride+width/2]>>24)!=255)return false;
        RECT owner{},edit{};GetWindowRect(window,&owner);GetWindowRect(shelf.search,&edit);if(edit.left<owner.left||edit.right>owner.right||edit.top<owner.top||edit.bottom>owner.bottom)return false;
        SetWindowTextW(shelf.search,L"中文 search");wchar_t text[40]{};GetWindowTextW(shelf.search,text,40);if(wcscmp(text,L"中文 search"))return false;
    }
    HWND search=shelf.search;DestroyWindow(window);return !shelf.window&&!IsWindow(search)&&!shelf.surface.pixels();
}
bool transition_tests() {
    if(shelf.lease||shelf.window||shelf.stop) return false;auto wanted=g_app.settings.organizer_enabled;bool prior=shelf.stopping;auto token=shelf.token;
    scope_exit restore([&]{shelf.lease.reset();shelf.stop.reset();shelf.stopping=prior;shelf.token=token;g_app.settings.organizer_enabled=wanted;});
    shelf.lease.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!shelf.lease) return false;shelf.token=42;shelf.stopping=true;g_app.settings.organizer_enabled=1;
    if(!message(WM_APP+91,42,HRESULT_FROM_WIN32(ERROR_BUSY))||!g_app.settings.organizer_enabled) return false;
    shelf.stopping=false;if(!message(WM_APP+91,41,HRESULT_FROM_WIN32(ERROR_BUSY))||!g_app.settings.organizer_enabled) return false;
    shelf.stop.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!shelf.stop) return false;shell_restarted();
    return shelf.stopping&&g_app.settings.organizer_enabled&&WaitForSingleObject(shelf.stop.get(),0)==WAIT_OBJECT_0&&bool(shelf.lease);
}
namespace {
bool menu_probe_ok{};int menu_probe_mode{};
BOOL WINAPI test_popup(HMENU menu,UINT,int,int,int,HWND owner,RECT const*) {
    if(menu_probe_mode==2){menu_probe_ok=GetMenuState(menu,20,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,21,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,22,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,23,MF_BYCOMMAND)!=UINT(-1);return 0;}
    shelf_proc(shelf.window,WM_TIMER,3,0);shelf_proc(shelf.window,WM_TIMER,2,0);
    menu_probe_ok=shelf.menu&&shelf.model->entries.size()==1&&shelf.model->entries[0].loose&&shelf.loose.size()==1&&IsWindow(owner);
    if(menu_probe_mode==1){DestroyWindow(owner);shelf.loose.clear();++shelf.generation;}
    return 1;
}
}
bool interaction_tests(std::wstring const& directory) {
    if(shelf.window||shelf.lease||shelf.editor||!shelf.loose.empty())return false;
    auto old_model=std::move(shelf.model);auto old_roots=shelf.roots;auto old_settings=g_app.settings;auto old_generation=shelf.generation;bool old_stopping=shelf.stopping;auto old_popup=popup_menu;
    HWND window=CreateWindowExW(0,L"STATIC",L"Organizer isolated test",WS_POPUP,0,0,500,300,nullptr,nullptr,g_app.instance,nullptr);if(!window){shelf.model=std::move(old_model);return false;}
    scope_exit restore([&]{popup_menu=old_popup;for(auto const& item:shelf.loose)if(IsWindow(item->window))DestroyWindow(item->window);shelf.loose.clear();if(shelf.editor)DestroyWindow(shelf.editor);shelf.editor=nullptr;shelf.window=nullptr;shelf.search=nullptr;DestroyWindow(window);shelf.model=std::move(old_model);shelf.roots=std::move(old_roots);shelf.menu=shelf.dragging=shelf.drag_groups=shelf.drag_started=false;shelf.drag_group.clear();shelf.drag_entry.clear();shelf.tiles.clear();shelf.stopping=old_stopping;shelf.generation=old_generation;g_app.settings=old_settings;});
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;shelf.window=window;shelf.stopping=false;shelf.roots.clear();g_app.settings.organizer_auto_collect=1;shelf.model=std::make_unique<Model>(directory);shelf.model->initialize_groups();Entry entry;entry.path=(std::filesystem::path(directory)/L"untouched.txt").wstring();entry.name=L"untouched";entry.group=L"documents";entry.loose=true;entry.due={};shelf.model->entries.push_back(entry);if(!write_utf8_atomic(entry.path.c_str(),L"original")||!shelf.model->save())return false;
    auto loose=std::make_unique<Loose>();loose->path=entry.path;loose->window=CreateWindowExW(0,L"STATIC",L"Hidden loose fixture",WS_POPUP,0,0,80,90,nullptr,nullptr,g_app.instance,nullptr);if(!loose->window)return false;SetWindowLongPtrW(loose->window,GWLP_USERDATA,LPARAM(loose.get()));shelf.loose.push_back(std::move(loose));
    // Both timers must defer while any gesture/editor owns stable Entry identity.
    for(int mode=0;mode<5;++mode){shelf.menu=mode==0;shelf.dragging=mode==1;shelf.loose[0]->dragging=mode==2;shelf.drag_group=mode==3?L"documents":L"";shelf.editor=mode==4?window:nullptr;shelf_proc(window,WM_TIMER,3,0);shelf_proc(window,WM_TIMER,2,0);if(shelf.model->entries.size()!=1||!shelf.model->entries[0].loose||shelf.loose.empty())return false;}
    shelf.menu=shelf.dragging=false;shelf.loose[0]->dragging=false;shelf.drag_group.clear();shelf.editor=nullptr;
    popup_menu=test_popup;menu_probe_mode=0;menu_probe_ok=false;loose_proc(shelf.loose[0]->window,WM_CONTEXTMENU,0,0);if(!menu_probe_ok||shelf.model->entries[0].loose||shelf.menu)return false;
    shelf_proc(window,WM_APP+95,0,0);if(!shelf.loose.empty()||!std::filesystem::exists(entry.path,error))return false;
    shelf.tiles.push_back({RECT{0,0,100,100},0,false});menu_probe_mode=2;menu_probe_ok=false;context_menu(POINT{},0);if(!menu_probe_ok)return false;
    // Disable/recreate during a nested popup destroys its target; stale command
    // must be ignored even when the model still exists after window destruction.
    shelf.model->entries[0].loose=true;auto transient=std::make_unique<Loose>();transient->path=entry.path;transient->window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,80,90,nullptr,nullptr,g_app.instance,nullptr);if(!transient->window)return false;SetWindowLongPtrW(transient->window,GWLP_USERDATA,LPARAM(transient.get()));HWND target=transient->window;shelf.loose.push_back(std::move(transient));menu_probe_mode=1;menu_probe_ok=false;loose_proc(target,WM_CONTEXTMENU,0,0);if(!menu_probe_ok||!shelf.model->entries[0].loose||!shelf.loose.empty())return false;shelf.menu=false;
    // Idle timer now collects exactly once, without a visible window or desktop lease.
    shelf_proc(window,WM_TIMER,3,0);return !shelf.model->entries[0].loose&&std::filesystem::exists(entry.path,error);
}
}
