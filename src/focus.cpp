#include "focus.hpp"
#include "sqlite.hpp"
#include "organizer.hpp"
#include "focus_ui_api.h"
namespace lb::focus {
void Timer::start(int64_t now,int minutes,int rest) { stage=1;rest_minutes=std::clamp(rest,0,60);deadline=now+int64_t(std::clamp(minutes,1,240))*60000; }
bool Timer::advance(int64_t now) { if(!stage||now<deadline) return false;if(stage==1&&rest_minutes) { stage=2;deadline=now+int64_t(rest_minutes)*60000; }else { stage=0;deadline=0; }return true; }
namespace {
struct State {
    Timer timer;bool pause{true},saved_pause{},loaded{},visible{};std::wstring saved_path,status;DWORD token{};int minutes{25};int64_t session{};
    HWND window{},minutes_control{},rest{},pause_control{},clock{},start{},stop{},names{},name{},status_control{};
    std::vector<std::pair<int64_t,std::wstring>> profiles;
    std::unique_ptr<sql::Database> db;
} state;
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
int64_t now() { FILETIME time{};GetSystemTimeAsFileTime(&time);return int64_t((uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000; }
void open() {
    if(state.db) return;state.db=std::make_unique<sql::Database>(std::filesystem::path(g_app.data)/L"focus.db");state.db->exec("CREATE TABLE IF NOT EXISTS focus_state(id INTEGER PRIMARY KEY CHECK(id=1),stage INTEGER NOT NULL,deadline INTEGER NOT NULL,rest INTEGER NOT NULL,pause INTEGER NOT NULL,saved_path TEXT NOT NULL,saved_pause INTEGER NOT NULL);CREATE TABLE IF NOT EXISTS workspaces(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE,path TEXT NOT NULL,fit INTEGER NOT NULL,monitor INTEGER NOT NULL,enabled INTEGER NOT NULL,alignment INTEGER NOT NULL,view INTEGER NOT NULL,size INTEGER NOT NULL,opacity INTEGER NOT NULL,glass INTEGER NOT NULL,color INTEGER NOT NULL,icon INTEGER NOT NULL,group_order TEXT NOT NULL);");
    state.db->exec("CREATE TABLE IF NOT EXISTS focus_options(id INTEGER PRIMARY KEY CHECK(id=1),minutes INTEGER NOT NULL,session INTEGER NOT NULL);CREATE TABLE IF NOT EXISTS focus_sessions(id INTEGER PRIMARY KEY,started INTEGER NOT NULL,ended INTEGER NOT NULL DEFAULT 0,duration INTEGER NOT NULL DEFAULT 0,completed INTEGER NOT NULL DEFAULT 0);");
}
void persist() { open();sql::Statement save(*state.db,"INSERT OR REPLACE INTO focus_state VALUES(1,?,?,?,?,?,?)");save.bind(1,state.timer.stage);save.bind(2,state.timer.deadline);save.bind(3,state.timer.rest_minutes);save.bind(4,state.pause);save.bind(5,state.saved_path);save.bind(6,state.saved_pause);save.next();sql::Statement options(*state.db,"INSERT OR REPLACE INTO focus_options VALUES(1,?,?)");options.bind(1,state.minutes);options.bind(2,state.session);options.next(); }
void end_session(bool completed) { if(!state.session)return;open();sql::Statement update(*state.db,"UPDATE focus_sessions SET ended=?,duration=?,completed=? WHERE id=? AND ended=0");update.bind(1,now());update.bind(2,completed?int64_t(state.minutes)*60:std::clamp<int64_t>(int64_t(state.minutes)*60-(state.timer.remaining(now())+999)/1000,0,int64_t(state.minutes)*60));update.bind(3,completed);update.bind(4,state.session);update.next();state.session=0; }
void restore_pause() { if(!state.saved_path.empty()&&!_wcsicmp(state.saved_path.c_str(),g_app.wallpaper.pending_path)) player_pause_desktop(state.saved_pause);state.saved_path.clear();state.token=0; }
void enforce() {
    auto &player=g_app.wallpaper;if(state.timer.stage!=1||!state.pause||!player.ready||player.closing||player.token==state.token) return;
    if(_wcsicmp(state.saved_path.c_str(),player.pending_path)) { state.saved_path=player.pending_path;state.saved_pause=player.paused;persist(); }state.token=player.token;player_pause_desktop(true);
}
void notify(wchar_t const* zh,wchar_t const* en) { auto tray=g_app.tray;tray.uFlags=NIF_INFO;StringCchCopyW(tray.szInfoTitle,ARRAYSIZE(tray.szInfoTitle),tr(L"轻块桌面 · 专注",L"Light Block · Focus"));StringCchCopyW(tray.szInfo,ARRAYSIZE(tray.szInfo),tr(zh,en));tray.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&tray); }
void refresh() {
    if(!state.window) return;auto remaining=(state.timer.remaining(now())+999)/1000;wchar_t value[100];StringCchPrintfW(value,100,L"%s  %02lld:%02lld",state.timer.stage==1?tr(L"专注中",L"Focus"):state.timer.stage==2?tr(L"休息中",L"Break"):tr(L"准备开始",L"Ready"),remaining/60,remaining%60);SetWindowTextW(state.clock,value);EnableWindow(state.stop,state.timer.stage!=0);EnableWindow(state.start,state.timer.stage==0);
}
void schedule() { KillTimer(g_app.window,21);if(state.timer.stage) SetTimer(g_app.window,21,UINT(std::clamp<int64_t>(state.timer.remaining(now()),100,state.visible&&g_app.page==1?1000:2147483647LL)),nullptr);refresh(); }
void tick() { if(state.timer.stage==1&&state.timer.remaining(now())==0)end_session(true);if(state.timer.advance(now())) { restore_pause();persist();notify(state.timer.stage==2?L"专注时间已结束，休息一下。":L"本次专注已完成。",state.timer.stage==2?L"Focus complete. Take a break.":L"Your focus session is complete."); }schedule(); }
void start_session(int minutes,int rest,bool pause) { if(state.timer.stage)return;if(minutes<1||minutes>180||rest<0||rest>60)throw std::runtime_error("Focus: 1-180 minutes; break: 0-60 minutes");open();auto prior_timer=state.timer;int prior_minutes=state.minutes;bool prior_pause=state.pause;auto prior_session=state.session;scope_exit rollback([&]{state.timer=prior_timer;state.minutes=prior_minutes;state.pause=prior_pause;state.session=prior_session;});sql::Transaction transaction(*state.db);state.minutes=minutes;state.pause=pause;sql::Statement add(*state.db,"INSERT INTO focus_sessions(started) VALUES(?)");add.bind(1,now());add.next();sql::Statement id(*state.db,"SELECT last_insert_rowid()");id.next();state.session=id.integer(0);state.timer.start(now(),minutes,rest);state.token=0;persist();transaction.commit();rollback.release();enforce();schedule(); }
void stop_session() { end_session(false);restore_pause();state.timer.stage=0;state.timer.deadline=0;persist();schedule(); }
void profiles() {
    open();state.profiles.clear();SendMessageW(state.names,CB_RESETCONTENT,0,0);sql::Statement rows(*state.db,"SELECT id,name FROM workspaces ORDER BY name LIMIT 50");while(rows.next()) { state.profiles.emplace_back(rows.integer(0),rows.text(1));SendMessageW(state.names,CB_ADDSTRING,0,LPARAM(state.profiles.back().second.c_str())); }if(!state.profiles.empty()) SendMessageW(state.names,CB_SETCURSEL,0,0);
}
void save_workspace(wchar_t const* supplied=nullptr) {
    wchar_t name[161];if(supplied)StringCchCopyW(name,161,supplied);else GetWindowTextW(state.name,name,161);if(!*name) throw std::runtime_error("Enter a workspace name");open();sql::Statement count(*state.db,"SELECT COUNT(*) FROM workspaces WHERE name<>?");count.bind(1,std::wstring(name));count.next();if(count.integer(0)>=50) throw std::runtime_error("Workspace limit reached");
    auto const& s=g_app.settings;organizer::Model model(g_app.data);model.load();std::wstring order;for(auto const& group:model.groups) { if(!order.empty()) order+=L'\n';order+=group.id; }
    sql::Statement save(*state.db,"INSERT OR REPLACE INTO workspaces(name,path,fit,monitor,enabled,alignment,view,size,opacity,glass,color,icon,group_order) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");save.bind(1,std::wstring(name));save.bind(2,std::wstring(s.active_path));save.bind(3,s.fit);save.bind(4,s.monitor);save.bind(5,s.organizer_enabled);save.bind(6,s.organizer_alignment);save.bind(7,s.organizer_view);save.bind(8,s.organizer_icon_size);save.bind(9,s.organizer_opacity);save.bind(10,s.organizer_glass);save.bind(11,int64_t(s.organizer_color));save.bind(12,int64_t(s.icon_color));save.bind(13,order);save.next();profiles();
}
int64_t selected() { auto n=int(SendMessageW(state.names,CB_GETCURSEL,0,0));return n>=0&&size_t(n)<state.profiles.size()?state.profiles[size_t(n)].first:0; }
void apply_workspace(int64_t id=0) {
    if(!id)id=selected();if(!id) return;sql::Statement row(*state.db,"SELECT path,fit,monitor,enabled,alignment,view,size,opacity,glass,color,icon,group_order FROM workspaces WHERE id=?");row.bind(1,id);if(!row.next()) return;auto path=row.text(0);if(!path.empty()&&(media_kind(path.c_str())==MEDIA_UNKNOWN||GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)) throw std::runtime_error("Workspace wallpaper was moved or removed");
    Settings previous=g_app.settings;auto &s=g_app.settings;s.fit=int(std::clamp<int64_t>(row.integer(1),0,3));s.monitor=int(std::clamp<int64_t>(row.integer(2),0,16));s.organizer_enabled=row.integer(3)!=0;s.organizer_alignment=int(std::clamp<int64_t>(row.integer(4),0,2));s.organizer_view=int(std::clamp<int64_t>(row.integer(5),0,2));s.organizer_icon_size=int(std::clamp<int64_t>(row.integer(6),0,2));s.organizer_opacity=int(std::clamp<int64_t>(row.integer(7),30,255));s.organizer_glass=row.integer(8)!=0;s.organizer_color=COLORREF(row.integer(9)&0xffffff);s.icon_color=COLORREF(row.integer(10)&0xffffff);s.organizer_custom_position=0;
    if(!save_config()) { g_app.settings=previous;throw std::runtime_error("Could not save workspace settings"); }
    auto order=row.text(11);organizer::Model model(g_app.data);if(model.load()) { std::vector<organizer::Group> groups;size_t offset=0;while(offset<order.size()) { size_t end=order.find(L'\n',offset);if(end==std::wstring::npos) end=order.size();auto id=order.substr(offset,end-offset);auto found=std::find_if(model.groups.begin(),model.groups.end(),[&](auto const& group){return group.id==id;});if(found!=model.groups.end()) { groups.push_back(*found);model.groups.erase(found); }offset=end+1; }groups.insert(groups.end(),model.groups.begin(),model.groups.end());model.groups=std::move(groups);model.save(); }
    organizer::configure();settings_close();if(!path.empty()) player_request(path.c_str(),TRUE);else player_refresh_settings();
}
HWND control(wchar_t const* cls,wchar_t const* text,DWORD style,int id) { auto child=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,state.window,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);SendMessageW(child,WM_SETFONT,WPARAM(g_app.font),TRUE);return child; }
void position() {
    RECT r{};GetClientRect(state.window,&r);int width=r.right;
    MoveWindow(state.clock,px(12),0,width-px(24),px(66),TRUE);
    for(int id=1;id<=4;++id) MoveWindow(GetDlgItem(state.window,id),px(12+(id-1)*146),px(85),px(134),px(34),TRUE);
    MoveWindow(state.pause_control,px(12),px(139),width-px(24),px(34),TRUE);MoveWindow(state.start,px(12),px(194),px(150),px(38),TRUE);MoveWindow(state.stop,px(174),px(194),px(150),px(38),TRUE);
    MoveWindow(GetDlgItem(state.window,10),px(12),px(270),width-px(24),px(38),TRUE);MoveWindow(state.name,px(12),px(323),std::max(px(120),width-px(170)),px(34),TRUE);MoveWindow(GetDlgItem(state.window,12),width-px(146),px(323),px(134),px(34),TRUE);
    MoveWindow(state.names,px(12),px(377),std::max(px(120),width-px(320)),px(240),TRUE);MoveWindow(GetDlgItem(state.window,14),width-px(296),px(377),px(134),px(34),TRUE);MoveWindow(GetDlgItem(state.window,15),width-px(146),px(377),px(134),px(34),TRUE);MoveWindow(state.status_control,px(12),px(435),width-px(24),px(80),TRUE);
}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE:state.window=window;state.clock=control(L"STATIC",L"",0,20);SendMessageW(state.clock,WM_SETFONT,WPARAM(g_app.title),TRUE);control(L"STATIC",tr(L"专注（分钟）",L"Focus minutes"),0,1);state.minutes_control=control(L"EDIT",L"25",WS_BORDER|ES_NUMBER|WS_TABSTOP,2);SendMessageW(state.minutes_control,EM_SETLIMITTEXT,3,0);control(L"STATIC",tr(L"休息（分钟）",L"Break minutes"),0,3);state.rest=control(L"EDIT",L"5",WS_BORDER|ES_NUMBER|WS_TABSTOP,4);SendMessageW(state.rest,EM_SETLIMITTEXT,2,0);
        state.pause_control=control(L"BUTTON",tr(L"专注期间暂停动态壁纸",L"Pause wallpaper during focus"),BS_AUTOCHECKBOX|WS_TABSTOP,5);SendMessageW(state.pause_control,BM_SETCHECK,state.pause?BST_CHECKED:BST_UNCHECKED,0);state.start=control(L"BUTTON",tr(L"开始专注",L"Start focus"),WS_TABSTOP,6);state.stop=control(L"BUTTON",tr(L"结束",L"Stop"),WS_TABSTOP,7);control(L"STATIC",tr(L"工作区 · 保存壁纸与收纳布局",L"Workspaces · wallpaper and organizer layout"),0,10);state.name=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,11);SendMessageW(state.name,EM_SETLIMITTEXT,160,0);control(L"BUTTON",tr(L"保存当前",L"Save current"),WS_TABSTOP,12);state.names=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,13);control(L"BUTTON",tr(L"应用",L"Apply"),WS_TABSTOP,14);control(L"BUTTON",tr(L"删除配置",L"Delete profile"),WS_TABSTOP,15);state.status_control=control(L"STATIC",L"",0,21);return 0;
    case WM_DESTROY:
        if(state.window==window) {
            state.window=state.minutes_control=state.rest=state.pause_control=state.clock=state.start=state.stop=state.names=state.name=state.status_control=nullptr;
            state.visible=false;state.profiles.clear();schedule();
        }return 0;
    case WM_SHOWWINDOW:state.visible=wp!=0;schedule();break;
    case WM_SIZE:position();return 0;
    case WM_COMMAND:try {
        int id=LOWORD(wp);if(id==6) { if(state.timer.stage) return 0;wchar_t m[6],r[6];GetWindowTextW(state.minutes_control,m,6);GetWindowTextW(state.rest,r,6);int minutes=_wtoi(m),rest=_wtoi(r);if(minutes<1||minutes>240||rest<0||rest>60) throw std::runtime_error("Focus: 1-240 minutes; break: 0-60 minutes");start_session(minutes,rest,SendMessageW(state.pause_control,BM_GETCHECK,0,0)==BST_CHECKED); }
        else if(id==7) { stop_session(); }
        else if(id==12) { save_workspace();SetWindowTextW(state.status_control,tr(L"工作区已保存。",L"Workspace saved.")); }
        else if(id==14) { apply_workspace();SetWindowTextW(state.status_control,tr(L"工作区已应用。",L"Workspace applied.")); }
        else if(id==15&&selected()) { sql::Statement remove(*state.db,"DELETE FROM workspaces WHERE id=?");remove.bind(1,selected());remove.next();profiles(); }
    }catch(std::exception const& error) { try { SetWindowTextW(state.status_control,sql::wide(error.what()).c_str()); }catch(...) {} }catch(...) { SetWindowTextW(state.status_control,tr(L"操作未完成。",L"Operation failed.")); }return 0;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:SetTextColor(HDC(wp),g_app.dark?RGB(235,240,249):RGB(29,38,49));SetBkColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: { RECT r{};GetClientRect(window,&r);SetDCBrushColor(HDC(wp),g_app.dark?RGB(18,23,32):RGB(246,247,250));FillRect(HDC(wp),&r,HBRUSH(GetStockObject(DC_BRUSH)));return 1; }
    }return DefWindowProcW(window,message,wp,lp);
}
}
void restore() {
    if(state.loaded) return;state.loaded=true;try { open();sql::Statement options(*state.db,"SELECT minutes,session FROM focus_options WHERE id=1");if(options.next()){state.minutes=int(std::clamp<int64_t>(options.integer(0),1,180));state.session=options.integer(1);}sql::Statement row(*state.db,"SELECT stage,deadline,rest,pause,saved_path,saved_pause FROM focus_state WHERE id=1");if(row.next()) { state.timer.stage=int(std::clamp<int64_t>(row.integer(0),0,2));state.timer.deadline=row.integer(1);state.timer.rest_minutes=int(std::clamp<int64_t>(row.integer(2),0,60));state.pause=row.integer(3)!=0;state.saved_path=row.text(4);state.saved_pause=row.integer(5)!=0;if(state.timer.deadline>now()+240LL*60000) state.timer.stage=0; }tick(); }catch(...) { state.db.reset();state.timer.stage=0;log_line(L"focus state unavailable; timer not resumed"); }
}
void show() {
    restore();g_app.page=1;state.visible=true;if(!state.window) { WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=proc;cls.lpszClassName=L"LightBlockDesktopNative.Focus";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,g_app.window,nullptr,g_app.instance,nullptr); }try { profiles(); }catch(...) { SetWindowTextW(state.status_control,tr(L"无法读取工作区数据库。",L"Workspace database is unavailable.")); }layout();schedule();
}
void home() { state.visible=false;layout();schedule(); }
void layout() { if(!state.window) return; if(GetParent(state.window)!=g_app.window) { RECT r{};GetClientRect(GetParent(state.window),&r);MoveWindow(state.window,0,0,r.right,r.bottom,TRUE);return; }bool visible=state.visible&&g_app.page==1;ShowWindow(state.window,visible?SW_SHOW:SW_HIDE);if(visible) { RECT r{};GetClientRect(g_app.window,&r);MoveWindow(state.window,px(236),px(137),std::max(px(340),int(r.right)-px(270)),std::max(px(180),int(r.bottom)-px(159)),TRUE); } }
bool message(UINT message,WPARAM wp,LPARAM) {
    if(message==WM_TIMER&&wp==21) { try { tick(); }catch(...) { KillTimer(g_app.window,21);set_status(L"专注状态保存失败，请查看日志。",L"Focus state could not be saved. See the log."); }return true; }
    if(message==WM_LB_READY) { try { enforce(); }catch(...) { log_line(L"focus pause could not be saved"); } }return false;
}
void shutdown() { if(state.window) DestroyWindow(state.window);KillTimer(g_app.window,21);state.window=nullptr;state.db.reset(); }
bool tests() { Timer timer;timer.start(1000,25,5);if(timer.stage!=1||timer.remaining(1000)!=1500000||timer.advance(1001)) return false;if(!timer.advance(1501000)||timer.stage!=2||timer.remaining(1501000)!=300000) return false;if(!timer.advance(1801000)||timer.stage||timer.remaining(1801000)) return false;timer.start(0,1,0);return timer.advance(60000)&&!timer.stage; }

HWND embed(HWND parent) { if(!IsWindow(state.window)) state.window=nullptr;show();SetParent(state.window,parent);layout();return state.window; }
namespace {
int ui_read(UiFocusState* out) {
    if(!out)return 0;*out={};try {restore();out->stage=state.timer.stage;out->minutes=state.minutes;out->rest=state.timer.rest_minutes;out->pause=state.pause;out->seconds=state.timer.stage?(state.timer.remaining(now())+999)/1000:int64_t(state.minutes)*60;open();sql::Statement stats(*state.db,"SELECT COUNT(*),COALESCE(SUM(duration),0) FROM focus_sessions WHERE completed=1");if(stats.next()){out->rounds=stats.integer(0);out->completed_seconds=stats.integer(1);}StringCchCopyW(out->status,300,state.status.c_str());return 1;}catch(...){return 0;}
}
int ui_workspace(int index,UiWorkspace* out) {
    if(!out||index<0||index>=50)return 0;*out={};try {open();sql::Statement row(*state.db,"SELECT id,name,path,alignment,view FROM workspaces ORDER BY name LIMIT 1 OFFSET ?");row.bind(1,index);if(!row.next())return 0;out->id=row.integer(0);StringCchCopyW(out->name,180,row.text(1).c_str());auto path=row.text(2);auto name=path.empty()?std::wstring(tr(L"保持当前壁纸",L"Keep current wallpaper")):std::filesystem::path(path).filename().wstring();wchar_t const* positions[]={tr(L"居上",L"Top"),tr(L"居中",L"Center"),tr(L"居下",L"Bottom")};StringCchPrintfW(out->description,500,L"%s · %s · %s",name.c_str(),positions[std::clamp<int64_t>(row.integer(3),0,2)],row.integer(4)==2?tr(L"分组",L"Groups"):tr(L"图标列表",L"Icon list"));return 1;}catch(...){return 0;}
}
int ui_action(int action,int64_t id,wchar_t const* name,int minutes,int rest,int pause) {
    try {restore();open();if(action==0)start_session(minutes,rest,pause!=0);else if(action==1)stop_session();else if(action==2){if(!name||!*name||wcslen(name)>160)return 0;save_workspace(name);}else if(action==3)apply_workspace(id);else if(action==4){if(id<=0)return 0;sql::Statement remove(*state.db,"DELETE FROM workspaces WHERE id=?");remove.bind(1,id);remove.next();profiles();}else return 0;state.status=tr(L"已保存。",L"Saved.");return 1;}catch(std::exception const& error){state.status=sql::wide(error.what());return 0;}catch(...){state.status=tr(L"操作未完成，请重试。",L"Could not complete. Please retry.");return 0;}
}
}
void const* ui_api() {static UiFocusApi const api{LB_FOCUS_UI_ABI,sizeof(UiFocusApi),ui_read,ui_workspace,ui_action};return &api;}
bool service_tests(std::filesystem::path const& directory) {
    if(state.window||state.timer.stage)return false;auto prior=std::move(state);auto data=std::wstring(g_app.data);state={};
    scope_exit cleanup([&]{state.db.reset();state=std::move(prior);StringCchCopyW(g_app.data,LB_PATH,data.c_str());});
    try {std::filesystem::create_directories(directory);StringCchCopyW(g_app.data,LB_PATH,directory.c_str());state.loaded=true;open();sql::Statement row(*state.db,"INSERT INTO focus_sessions(started) VALUES(?)");row.bind(1,now()-1500000);row.next();sql::Statement id(*state.db,"SELECT last_insert_rowid()");id.next();state.session=id.integer(0);state.minutes=25;end_session(true);persist();state.loaded=false;state.db.reset();UiFocusState read{};if(!ui_read(&read)||read.rounds!=1||read.completed_seconds!=1500||read.seconds!=1500)return false;
        if(!ui_action(2,0,L"本地测试工作区",0,0,0))return false;UiWorkspace workspace{};if(!ui_workspace(0,&workspace)||wcscmp(workspace.name,L"本地测试工作区")||!workspace.description[0])return false;
        if(!ui_action(4,workspace.id,nullptr,0,0,0)||ui_workspace(0,&workspace))return false;return !ui_action(0,0,nullptr,0,5,0);
    }catch(...){return false;}
}
}
