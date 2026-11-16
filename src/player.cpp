#include "lb.h"
#include "static_wallpaper.hpp"

namespace {
std::atomic<DWORD> serial{1};
bool last_desktop{};
bool quitting{};
bool restore_pending{};
int static_fit{-1};
bool qt_preview{},qt_wallpaper{};
std::wstring deleting_path;
bool deletion_restoring{},deletion_failed{},deletion_allow_missing_backup{};
std::wstring running_preview,running_wallpaper;
struct ResultSlot{Player player;HWND owner{};std::wstring running;bool qt{},retired{};};
std::vector<std::unique_ptr<ResultSlot>> result_slots;
ResultSlot* result_slot(Player const& p){for(auto& slot:result_slots)if(&slot->player==&p)return slot.get();return nullptr;}
std::wstring& running_path(Player& p) {if(auto slot=result_slot(p))return slot->running;return &p==&g_app.wallpaper?running_wallpaper:running_preview;}
bool& qt_backend(Player& p) {if(auto slot=result_slot(p))return slot->qt;return &p==&g_app.wallpaper?qt_wallpaper:qt_preview;}
class attribute_list {
    std::unique_ptr<std::byte[]> memory_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
    HANDLE inherited_{};
public:
    explicit attribute_list(HANDLE inherited) noexcept:inherited_(inherited) {
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);if(!bytes) return;
        memory_.reset(new(std::nothrow) std::byte[bytes]);if(!memory_) return;
        auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory_.get());
        if(!InitializeProcThreadAttributeList(list,1,0,&bytes)) return;list_=list;
        if(!UpdateProcThreadAttribute(list_,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&inherited_,sizeof inherited_,nullptr,nullptr)) { DeleteProcThreadAttributeList(list_);list_=nullptr; }
    }
    ~attribute_list() { if(list_) DeleteProcThreadAttributeList(list_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
};
void release_child(Player &p) noexcept {
    running_path(p).clear();
    p.process.reset();p.job.reset();p.window=nullptr;p.pid=0;p.ready=FALSE;p.closing=0;
}
bool launch_child(Player &p) {
    if(p.pending_desktop&&media_kind(p.pending_path)==MEDIA_IMAGE) {
        if(!lb::static_wallpaper::request(p.pending_path)) return false;
        static_fit=g_app.settings.fit;
        p.pending=FALSE;p.ready=FALSE;p.failed=FALSE;p.paused=FALSE;p.muted=TRUE;return true;
    }
    if(p.embed && !IsWindow(p.embed)) { p.pending=FALSE;return false; }
    wchar_t executable[LB_PATH*2+4],file[LB_PATH*2+4],data[LB_PATH*2+4],command[LB_PATH*6+800];
    if(!quote_arg(executable,ARRAYSIZE(executable),g_app.exe)||!quote_arg(file,ARRAYSIZE(file),p.pending_path)||!quote_arg(data,ARRAYSIZE(data),g_app.data)) return false;
    p.token=serial.fetch_add(1);if(!p.token) p.token=serial.fetch_add(1);
    lb::unique_handle parent(OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId()));HANDLE inherited_raw=nullptr;
    if(!parent||!DuplicateHandle(GetCurrentProcess(),parent.get(),GetCurrentProcess(),&inherited_raw,0,TRUE,DUPLICATE_SAME_ACCESS)) return false;
    lb::unique_handle inherited(inherited_raw);attribute_list attributes(inherited.get());if(!attributes.get()) return false;
    auto slot=result_slot(p);HWND owner=slot?slot->owner:g_app.window;
    HRESULT hr=StringCchPrintfW(command,ARRAYSIZE(command),L"%s --worker --path %s --data-dir %s --owner %llu --token %lu --parent-handle %llu --embed %llu %s %s %s",executable,file,data,reinterpret_cast<unsigned long long>(owner),p.token,reinterpret_cast<unsigned long long>(inherited.get()),reinterpret_cast<unsigned long long>(p.embed),p.pending_desktop?L"--desktop":L"",qt_backend(p)?L"--qt-media":L"",GetPropW(g_app.window,L"LB.HiddenProbe")?L"--hidden-probe":L"");
    if(FAILED(hr)) return false;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.lpAttributeList=attributes.get();
    lb::unique_handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof limits)) return false;
    PROCESS_INFORMATION info{};if(!CreateProcessW(g_app.exe,command,nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED,nullptr,g_app.directory,&startup.StartupInfo,&info)) return false;
    lb::unique_handle process(info.hProcess),thread(info.hThread);
    if(!AssignProcessToJobObject(job.get(),process.get())) { TerminateProcess(process.get(),2);return false; }
    if(ResumeThread(thread.get())==DWORD(-1)) return false;
    p.process=std::move(process);p.job=std::move(job);p.pid=info.dwProcessId;p.started=GetTickCount64();p.pending=FALSE;p.closing=0;p.ready=FALSE;p.failed=FALSE;
    running_path(p)=p.pending_path;
    p.paused=FALSE;p.muted=TRUE;p.position=0;p.duration=0;
    if(!result_slot(p)&&(&p==&g_app.wallpaper)==last_desktop) { g_app.paused=p.paused;g_app.muted=p.muted; }
    if(!slot){SetPropW(g_app.window,L"LB.PlayerPID",reinterpret_cast<HANDLE>(uintptr_t(p.pid)));SetPropW(g_app.window,L"LB.Ready",nullptr);set_status(L"正在打开媒体…",L"Opening media…");}log_line(L"worker launch pid=%lu token=%lu desktop=%d",p.pid,p.token,p.pending_desktop);return true;
}
void stop(Player &p,bool clear) {
    if(clear) p.pending=FALSE;
    if(clear&&!p.process) p.ready=FALSE;
    if(p.process && !p.closing) { p.closing=GetTickCount64();if(p.window) PostMessageW(p.window,WM_CLOSE,0,0);SetTimer(g_app.window,1,80,nullptr); }
}
void request(Player &p,const wchar_t *path,bool desktop,HWND embed=nullptr) {
    bool result_preview=result_slot(p)!=nullptr;
    if(!path || GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES) { if(!result_preview)error_box(g_app.window,tr(L"找不到媒体文件",L"Media file not found"),HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));return; }
    if(path!=p.pending_path && FAILED(StringCchCopyW(p.pending_path,LB_PATH,path))) return;
    p.pending=TRUE;p.pending_desktop=desktop;p.embed=embed;
    qt_backend(p)=false;
    if(desktop&&lb::static_wallpaper::busy()) lb::static_wallpaper::cancel();
    if(p.process) stop(p,false);else if(!(desktop&&lb::static_wallpaper::busy())&&!launch_child(p)) { p.pending=FALSE;if(!result_preview)error_box(g_app.window,tr(L"无法启动壁纸进程",L"Cannot start wallpaper process"),HRESULT_FROM_WIN32(GetLastError())); }
    SetTimer(g_app.window,1,80,nullptr);
}
bool fallback(Player& p) {
    if(qt_backend(p)||media_kind(p.pending_path)!=MEDIA_VIDEO||quitting||p.pending||p.closing||GetPropW(g_app.window,L"LB.HiddenProbe")) return false;
    qt_backend(p)=true;p.pending=TRUE;p.failed=FALSE;stop(p,false);
    set_status(L"正在切换至内置兼容解码器…",L"Switching to the bundled compatibility decoder…");
    log_line(L"Media Foundation fallback requested pid=%lu",p.pid);return true;
}
void tick(Player &p,ULONGLONG now) {
    if(p.process) {
        if(WaitForSingleObject(p.process.get(),0)==WAIT_OBJECT_0) {
            DWORD code=0;GetExitCodeProcess(p.process.get(),&code);log_line(L"worker exit pid=%lu code=%lu",p.pid,code);
            if(code&&!p.closing&&!p.pending) fallback(p);
            if(code && !p.closing && !p.pending) { p.failed=TRUE;set_status(L"播放进程异常退出，可以重新预览",L"The player exited unexpectedly. You can preview again."); }
            release_child(p);if(!p.pending && !p.failed) set_status(L"播放已停止，系统壁纸保持原样",L"Playback stopped. System wallpaper is unchanged.");
        } else if(p.closing && now-p.closing>1800) { log_line(L"worker shutdown timeout pid=%lu",p.pid);TerminateJobObject(p.job.get(),3); }
        else if(!p.closing && !p.ready && now-p.started>15000) { if(!fallback(p)) {p.failed=TRUE;set_status(L"打开超时，已取消；可以尝试其他媒体",L"Opening timed out. Try a different file.");stop(p,true);} }
    }
    if(!p.process && p.pending && !quitting && !(&p==&g_app.wallpaper&&lb::static_wallpaper::busy()) && !launch_child(p)) { p.pending=FALSE;set_status(L"播放进程启动失败，请查看日志",L"Player could not start. See the log."); }
}
}
void player_request(const wchar_t *path,BOOL desktop) {
    if(desktop&&deletion_restoring) {set_status(L"正在恢复系统壁纸以安全删除，完成后请再应用",L"Restoring the system wallpaper for safe deletion. Apply again once it finishes.");return;}
    if(path&&!deleting_path.empty()&&!_wcsicmp(path,deleting_path.c_str())&&!deletion_failed) {set_status(L"正在释放这张壁纸，完成后即可删除",L"Releasing this wallpaper before deleting it.");return;}
    if(desktop) restore_pending=false;
    last_desktop=desktop!=FALSE;auto &state=desktop?g_app.wallpaper:g_app.player;
    g_app.paused=state.paused;g_app.muted=state.muted;
    if(path&&state.ready&&!state.closing&&!state.pending&&!state.embed&&!_wcsicmp(path,state.pending_path)) return;
    request(state,path,desktop!=FALSE);
}
void player_preview_embed(const wchar_t *path,HWND host) { if(path&&!deleting_path.empty()&&!_wcsicmp(path,deleting_path.c_str())&&!deletion_failed) return;last_desktop=false;g_app.paused=g_app.player.paused;g_app.muted=g_app.player.muted;request(g_app.player,path,false,host); }
/* The lifecycle regression exercises both slots using ordinary preview windows. */
void player_test_secondary(const wchar_t *path) { request(g_app.wallpaper,path,false); }
void player_stop_preview(void) { stop(g_app.player,true); }
Player* player_result_state(HWND host){for(auto& slot:result_slots)if(slot->player.embed==host&&!slot->retired)return &slot->player;return nullptr;}
size_t player_result_slots(){return std::count_if(result_slots.begin(),result_slots.end(),[](auto const& slot){return slot->player.process||slot->player.pending;});}
bool player_result_releasing(){return std::any_of(result_slots.begin(),result_slots.end(),[](auto const& slot){return (slot->retired||slot->player.closing)&&(slot->player.process||slot->player.pending);});}
BOOL player_result_request(wchar_t const* path,HWND host,HWND owner){
    if(quitting||!IsWindow(host)||!IsWindow(owner)||!path)return FALSE;
    auto attributes=GetFileAttributesW(path);if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))return FALSE;
    if(auto existing=player_result_state(host)){if(!existing->process&&!existing->pending&&player_result_slots()>=2)return FALSE;if(existing->ready&&!existing->closing&&!wcscmp(existing->pending_path,path))return TRUE;request(*existing,path,false,host);return existing->process||existing->pending;}
    if(player_result_slots()>=2)return FALSE;auto slot=std::make_unique<ResultSlot>();slot->owner=owner;slot->player.embed=host;auto pointer=slot.get();result_slots.push_back(std::move(slot));request(pointer->player,path,false,host);if(!pointer->player.process&&!pointer->player.pending){result_slots.pop_back();return FALSE;}return TRUE;
}
void player_result_stop(HWND host){for(auto& slot:result_slots)if(slot->player.embed==host){slot->retired=true;stop(slot->player,true);}SetTimer(g_app.window,1,80,nullptr);}
void player_forget(const wchar_t *path) {
    if(!path) return;deleting_path=path;deletion_failed=false;deletion_restoring=false;
    bool active=!_wcsicmp(g_app.settings.active_path,path),reading=lb::static_wallpaper::reading(path);
    bool original=lb::static_wallpaper::has_original();deletion_allow_missing_backup=!original&&(!active||media_kind(path)!=MEDIA_IMAGE);
    for(auto *state:{&g_app.player,&g_app.wallpaper}) {
        bool queued=!_wcsicmp(state->pending_path,path),running=!running_path(*state).empty()&&!_wcsicmp(running_path(*state).c_str(),path);
        if(queued||running) stop(*state,queued||!state->pending);
    }
    for(auto& slot:result_slots)if(!_wcsicmp(slot->player.pending_path,path)||!_wcsicmp(slot->running.c_str(),path)){slot->retired=true;stop(slot->player,true);}
    if(reading) lb::static_wallpaper::cancel();
    if(active||reading) {
        stop(g_app.wallpaper,true);
        if(original||reading||media_kind(path)==MEDIA_IMAGE) {deletion_restoring=true;restore_pending=true;SetTimer(g_app.window,1,80,nullptr);}
        else {g_app.settings.active_path[0]=0;if(!save_config()) deletion_failed=true;}
    }
}
bool player_uses_file(wchar_t const* path){if(!path)return false;if(lb::static_wallpaper::reading(path))return true;for(auto& slot:result_slots)if((slot->player.process&&!_wcsicmp(slot->running.c_str(),path))||(slot->player.pending&&!_wcsicmp(slot->player.pending_path,path)))return true;for(auto* state:{&g_app.player,&g_app.wallpaper})if((state->process&&!running_path(*state).empty()&&!_wcsicmp(path,running_path(*state).c_str()))||(!_wcsicmp(path,state->pending_path)&&state->pending))return true;return false;}
int player_file_released(wchar_t const* path) {
    if(!path) return -1;
    if(!deleting_path.empty()&&!_wcsicmp(path,deleting_path.c_str())) {if(deletion_failed) return -1;if(deletion_restoring) return 0;}
    if(lb::static_wallpaper::reading(path)) return 0;
    for(auto& slot:result_slots)if((slot->player.process&&!_wcsicmp(slot->running.c_str(),path))||(slot->player.pending&&!_wcsicmp(slot->player.pending_path,path)))return 0;
    for(auto* state:{&g_app.player,&g_app.wallpaper}) if((state->process&&!running_path(*state).empty()&&!_wcsicmp(path,running_path(*state).c_str()))||(!_wcsicmp(path,state->pending_path)&&state->pending)) return 0;
    if(!deleting_path.empty()&&!_wcsicmp(path,deleting_path.c_str())) {deleting_path.clear();deletion_failed=deletion_allow_missing_backup=false;}
    return 1;
}
void player_cancel_delete(wchar_t const* path) {
    if(path&&!deleting_path.empty()&&!_wcsicmp(path,deleting_path.c_str())) {deleting_path.clear();deletion_restoring=deletion_failed=deletion_allow_missing_backup=false;}
}
void player_stop(BOOL clear) { if(g_app.player.quitting) quitting=true;if(clear) {restore_pending=false;lb::static_wallpaper::cancel();if(deletion_restoring) {deletion_restoring=false;deletion_failed=true;}}stop(g_app.player,clear!=FALSE);stop(g_app.wallpaper,clear!=FALSE);if(quitting)for(auto& slot:result_slots){slot->retired=true;stop(slot->player,true);} }
void player_restore_wallpaper(void) {
    restore_pending=true;lb::static_wallpaper::cancel();stop(g_app.wallpaper,true);SetTimer(g_app.window,1,80,nullptr);
}
void player_tick(void) {
    ULONGLONG now=GetTickCount64();if(g_app.heartbeat) g_app.max_gap=std::max(g_app.max_gap,now-g_app.heartbeat);g_app.heartbeat=now;
    auto outcome=lb::static_wallpaper::tick();
    if(outcome.completed&&outcome.restored&&deletion_restoring&&!outcome.cancelled) {
        if(!outcome.success&&outcome.error==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)&&deletion_allow_missing_backup) outcome.success=true;
        deletion_restoring=false;deletion_failed=!outcome.success;
    }
    if(outcome.completed&&!outcome.cancelled) {
        if(outcome.success) {
            if(outcome.restored) { g_app.settings.active_path[0]=0;g_app.wallpaper.ready=FALSE;set_status(L"已恢复原系统壁纸",L"Original Windows wallpaper restored."); }
            else { StringCchCopyW(g_app.settings.active_path,LB_PATH,outcome.path.c_str());g_app.wallpaper.ready=TRUE;set_status(L"Windows 原生静态壁纸 · 退出后仍保留",L"Windows static wallpaper · Kept after the app exits."); }
            if(!save_config()) {if(outcome.restored&&!deleting_path.empty()) deletion_failed=true;set_status(L"壁纸已应用，但设置未能保存，请检查存储目录",L"Wallpaper applied, but settings could not be saved. Check the data folder.");}
        }else { g_app.wallpaper.failed=TRUE;log_line(L"static wallpaper transaction failed hr=0x%08lX",DWORD(outcome.error));if(outcome.restored&&outcome.error==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) set_status(L"原系统壁纸文件已不存在，请在 Windows 设置中选择壁纸",L"The original wallpaper file no longer exists. Choose a wallpaper in Windows Settings.");else if(outcome.restored&&outcome.error==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) set_status(L"尚未更改系统静态壁纸，没有需要恢复的备份",L"No Windows static wallpaper has been changed; there is no backup to restore.");else set_status(L"静态壁纸切换未完成，原壁纸备份已保留，请重试",L"Static wallpaper could not be applied. The original backup is preserved; please retry."); }
    }
    tick(g_app.player,now);tick(g_app.wallpaper,now);
    for(auto& slot:result_slots){if(!IsWindow(slot->owner)||!IsWindow(slot->player.embed)){slot->retired=true;stop(slot->player,true);}tick(slot->player,now);}
    std::erase_if(result_slots,[](auto const& slot){return slot->retired&&!slot->player.process&&!slot->player.pending;});
    if(restore_pending&&!quitting&&!g_app.wallpaper.process&&!lb::static_wallpaper::busy()) { restore_pending=false;if(!lb::static_wallpaper::restore()) {if(deletion_restoring) {deletion_restoring=false;deletion_failed=true;}set_status(L"无法开始恢复系统壁纸",L"Could not start restoring the original wallpaper.");} }
    if(!g_app.player.process && !g_app.player.pending && !g_app.wallpaper.process && !g_app.wallpaper.pending&&result_slots.empty()&&!lb::static_wallpaper::busy()&&!restore_pending) { KillTimer(g_app.window,1);g_app.heartbeat=0;if(quitting) DestroyWindow(g_app.window); }
}
void player_message(UINT message,WPARAM token,LPARAM value) {
    Player *state=nullptr;
    for(auto *candidate:{&g_app.player,&g_app.wallpaper}) if(candidate->process && candidate->token==DWORD(token)) { state=candidate;break; }
    if(!state)for(auto& slot:result_slots)if(slot->player.process&&slot->player.token==DWORD(token)){state=&slot->player;break;}
    if(!state) return;Player &p=*state;
    if(message==WM_LB_HELLO) { DWORD pid=0;GetWindowThreadProcessId(reinterpret_cast<HWND>(value),&pid);if(pid!=p.pid) return;p.window=reinterpret_cast<HWND>(value);if(p.closing) PostMessageW(p.window,WM_CLOSE,0,0); }
    else if(message==WM_LB_READY && !p.closing) {
        p.ready=TRUE;log_line(L"first-frame ready pid=%lu elapsed_ms=%llu",p.pid,GetTickCount64()-p.started);
        if(p.pending_desktop) { StringCchCopyW(g_app.settings.active_path,LB_PATH,p.pending_path);save_config();if(g_app.settings.wallpaper_sound&&media_kind(p.pending_path)==MEDIA_VIDEO&&p.muted){p.muted=FALSE;PostMessageW(p.window,WM_LB_CONTROL,2,0);} }
        set_status(p.pending_desktop?L"动态壁纸已应用":L"预览中 · 空格暂停，Esc 关闭",p.pending_desktop?L"Animated wallpaper applied.":L"Preview · Space to pause, Esc to close");
    } else if(message==WM_LB_ERROR && !p.closing) {
        if(fallback(p)) return;
        p.failed=TRUE;log_line(L"worker error pid=%lu hr=0x%08lX",p.pid,DWORD(value));
        if(HRESULT(value)==HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND)) set_status(L"此 Windows 缺少原生视频组件；图片和 GIF 仍可使用",L"Windows media components are unavailable. Images and GIF still work.");
        else set_status(L"此文件暂不支持或打开失败；请尝试其他媒体",L"Unsupported or invalid file. Try another file.");stop(p,true);
    } else if(message==WM_LB_POSITION) p.position=int64_t(value);
    else if(message==WM_LB_DURATION) p.duration=int64_t(value);
    else if(message==WM_LB_STATE) { p.paused=(value&1)!=0;p.muted=(value&2)!=0;if(!result_slot(p)&&(&p==&g_app.wallpaper)==last_desktop) { g_app.paused=p.paused;g_app.muted=p.muted;InvalidateRect(g_app.window,nullptr,FALSE); } }
}
void player_control(int command) { Player &p=last_desktop?g_app.wallpaper:g_app.player;if(p.window) PostMessageW(p.window,WM_LB_CONTROL,WPARAM(command),0); }
void player_pause_desktop(bool paused) { auto &p=g_app.wallpaper;if(p.window&&!p.closing) { p.paused=paused;PostMessageW(p.window,WM_LB_CONTROL,5,paused); } }
Player& player_active(void) { return last_desktop?g_app.wallpaper:g_app.player; }
void player_settings_changed(void) { for(auto *p:{&g_app.player,&g_app.wallpaper}) if(p->process&&!p->closing) request(*p,p->pending_path,p->pending_desktop!=FALSE,p->embed);if(!g_app.wallpaper.process&&*g_app.settings.active_path&&media_kind(g_app.settings.active_path)==MEDIA_IMAGE) request(g_app.wallpaper,g_app.settings.active_path,true); }
void player_refresh_settings(void) { for(auto *state:{&g_app.player,&g_app.wallpaper}) if(state->window&&!state->closing) PostMessageW(state->window,WM_LB_CONTROL,4,0);if(static_fit!=g_app.settings.fit&&!g_app.wallpaper.process) { auto path=lb::static_wallpaper::busy()?g_app.wallpaper.pending_path:g_app.settings.active_path;if(*path&&media_kind(path)==MEDIA_IMAGE) request(g_app.wallpaper,path,true); } }
void player_dispose(void) { lb::static_wallpaper::dispose();for(auto& slot:result_slots){if(slot->player.job)TerminateJobObject(slot->player.job.get(),0);release_child(slot->player);}result_slots.clear();for(auto *p:{&g_app.player,&g_app.wallpaper}) { if(p->job) TerminateJobObject(p->job.get(),0);release_child(*p); } }
bool player_release_tests(wchar_t const* directory) {
    if(g_app.player.process||g_app.wallpaper.process||lb::static_wallpaper::busy()) return false;
    auto saved_settings=g_app.settings;auto saved_data=std::wstring(g_app.data),saved_config=std::wstring(g_app.config);HWND saved_window=g_app.window;
    auto old_preview=std::move(g_app.player),old_wallpaper=std::move(g_app.wallpaper);bool old_quitting=quitting;
    auto old_delete=deleting_path,old_running_preview=running_preview,old_running_wallpaper=running_wallpaper;
    bool old_restore=restore_pending,old_deletion_restoring=deletion_restoring,old_deletion_failed=deletion_failed,old_allow=deletion_allow_missing_backup;
    HWND fixture=CreateWindowExW(0,L"STATIC",L"",0,0,0,1,1,HWND_MESSAGE,nullptr,g_app.instance,nullptr);if(!fixture) return false;
    lb::scope_exit cleanup([&]{KillTimer(fixture,1);DestroyWindow(fixture);g_app.window=saved_window;g_app.player=std::move(old_preview);g_app.wallpaper=std::move(old_wallpaper);g_app.settings=saved_settings;StringCchCopyW(g_app.data,LB_PATH,saved_data.c_str());StringCchCopyW(g_app.config,LB_PATH,saved_config.c_str());quitting=old_quitting;restore_pending=old_restore;deleting_path=old_delete;deletion_restoring=old_deletion_restoring;deletion_failed=old_deletion_failed;deletion_allow_missing_backup=old_allow;running_preview=old_running_preview;running_wallpaper=old_running_wallpaper;});
    g_app.window=fixture;g_app.player={};g_app.wallpaper={};quitting=false;g_app.settings.active_path[0]=0;StringCchCopyW(g_app.data,LB_PATH,directory);ensure_directory(directory);path_join(g_app.config,LB_PATH,directory,L"settings.ini");
    wchar_t first[LB_PATH],next[LB_PATH];path_join(first,LB_PATH,directory,L"current.png");path_join(next,LB_PATH,directory,L"next.mp4");
    g_app.player.process.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));running_preview=first;StringCchCopyW(g_app.player.pending_path,LB_PATH,next);g_app.player.pending=TRUE;
    player_forget(first);if(player_file_released(first)!=0||!g_app.player.pending) return false;
    release_child(g_app.player);if(player_file_released(first)!=1) return false;g_app.player.pending=FALSE;
    StringCchCopyW(g_app.settings.active_path,LB_PATH,first);player_forget(first);
    if(player_file_released(first)!=0||!deletion_restoring||wcscmp(g_app.settings.active_path,first)) return false;
    player_stop(TRUE);if(player_file_released(first)!=-1||wcscmp(g_app.settings.active_path,first)) return false;
    g_app.settings.active_path[0]=0;player_forget(next);return player_file_released(next)==1&&deleting_path.empty();
}
bool player_result_tests(){
    if(!result_slots.empty())return false;auto original_preview=g_app.player.position,original_wallpaper=g_app.wallpaper.position;
    HWND owner=CreateWindowExW(0,L"STATIC",L"hidden results fixture",WS_POPUP,0,0,80,60,nullptr,nullptr,g_app.instance,nullptr);if(!owner)return false;
    HWND first=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,20,20,owner,nullptr,g_app.instance,nullptr),second=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,20,0,20,20,owner,nullptr,g_app.instance,nullptr),third=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,40,0,20,20,owner,nullptr,g_app.instance,nullptr);
    lb::scope_exit cleanup([&]{for(auto& slot:result_slots)release_child(slot->player);result_slots.clear();DestroyWindow(owner);});if(!first||!second||!third)return false;
    for(auto host:{first,second}){auto slot=std::make_unique<ResultSlot>();slot->owner=owner;slot->player.embed=host;slot->player.token=serial.fetch_add(1);slot->player.process.reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!slot->player.process)return false;result_slots.push_back(std::move(slot));}
    auto a=player_result_state(first),b=player_result_state(second);if(!a||!b||a==b||player_result_slots()!=2||player_result_request(g_app.exe,third,owner))return false;
    auto failed=std::make_unique<ResultSlot>();failed->owner=owner;failed->player.embed=third;failed->player.failed=TRUE;result_slots.push_back(std::move(failed));if(player_result_request(g_app.exe,third,owner)||player_result_slots()!=2)return false;result_slots.pop_back();
    player_message(WM_LB_POSITION,a->token,1234);player_message(WM_LB_POSITION,b->token,5678);player_message(WM_LB_STATE,a->token,3);if(a->position!=1234||b->position!=5678||!a->paused||!a->muted||b->paused||g_app.player.position!=original_preview||g_app.wallpaper.position!=original_wallpaper)return false;
    if(player_result_releasing())return false;result_slots.front()->retired=true;if(player_result_state(first)||player_result_state(second)!=b||player_result_slots()!=2||!player_result_releasing())return false;for(int n=0;n<5;++n)if(player_result_request(g_app.exe,third,owner)||b->closing||b->paused||player_result_slots()!=2)return false;release_child(result_slots.front()->player);if(player_result_slots()!=1||player_result_releasing())return false;
    release_child(*b);result_slots.clear();auto original_exe=std::wstring(g_app.exe);StringCchCopyW(g_app.exe,LB_PATH,L"missing-result-worker.exe");bool unexpected=player_result_request(original_exe.c_str(),third,owner)!=FALSE;StringCchCopyW(g_app.exe,LB_PATH,original_exe.c_str());if(unexpected||!result_slots.empty()||player_result_request(L"missing-result-source.mp4",third,owner))return false;return !IsWindowVisible(owner);
}
bool player_result_runtime_test(wchar_t const* source){
    if(!result_slots.empty()||g_app.player.process||g_app.wallpaper.process)return false;
    static POINT drag_point{};static unsigned drag_count{};drag_count=0;
    WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpszClassName=L"LightBlockDesktopNative.RecordingFrame";cls.lpfnWndProc=+[](HWND window,UINT message,WPARAM wp,LPARAM lp)->LRESULT{if(message==WM_NCLBUTTONDOWN){if(wp==HTCAPTION){drag_point={short(LOWORD(lp)),short(HIWORD(lp))};++drag_count;}return 0;}if(message>=WM_LB_HELLO&&message<=WM_LB_STATE){player_message(message,wp,lp);return 0;}return DefWindowProcW(window,message,wp,lp);};bool registered=RegisterClassW(&cls)!=0;
    HWND owner=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,110,80,320,90,nullptr,nullptr,g_app.instance,nullptr);if(!owner){if(registered)UnregisterClassW(cls.lpszClassName,g_app.instance);return false;}SetWindowLongPtrW(owner,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(cls.lpfnWndProc));
    auto old_window=g_app.window;bool old_quitting=quitting;auto old_preview=g_app.player.position,old_wallpaper=g_app.wallpaper.position;g_app.window=owner;quitting=false;SetPropW(owner,L"LB.HiddenProbe",HANDLE(1));
    lb::scope_exit cleanup([&]{for(auto& slot:result_slots){if(slot->player.job)TerminateJobObject(slot->player.job.get(),0);if(slot->player.process)WaitForSingleObject(slot->player.process.get(),2000);release_child(slot->player);}result_slots.clear();KillTimer(owner,1);DestroyWindow(owner);if(registered)UnregisterClassW(cls.lpszClassName,g_app.instance);g_app.window=old_window;quitting=old_quitting;});
    HWND first=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,160,90,owner,nullptr,g_app.instance,nullptr),second=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,160,0,160,90,owner,nullptr,g_app.instance,nullptr);if(!first||!second)return false;
    auto wait=[&](auto condition,int timeout){ULONGLONG began=GetTickCount64();do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}for(auto& slot:result_slots)tick(slot->player,GetTickCount64());std::erase_if(result_slots,[](auto const& slot){return slot->retired&&!slot->player.process&&!slot->player.pending;});if(condition())return true;Sleep(10);}while(GetTickCount64()-began<ULONGLONG(timeout));return false;};
    if(!player_result_request(source,first,owner)||!player_result_request(source,second,owner))return false;
    if(!wait([&]{auto a=player_result_state(first),b=player_result_state(second);return a&&b&&a->ready&&b->ready&&a->window&&b->window;},16000))return false;
    auto a=player_result_state(first),b=player_result_state(second);if(a->pid==b->pid||!a->pid||!b->pid)return false;DWORD first_pid=a->pid,second_pid=b->pid;
    for(auto player:{a,b}){POINT expected{12,18};if(!ClientToScreen(player->window,&expected))return false;unsigned before=drag_count;PostMessageW(player->window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,18));if(!wait([&]{return drag_count==before+1;},2000)||drag_point.x!=expected.x||drag_point.y!=expected.y)return false;}
    PostMessageW(a->window,WM_LB_CONTROL,5,TRUE);PostMessageW(b->window,WM_LB_CONTROL,5,TRUE);if(!wait([&]{return player_result_state(first)->paused&&player_result_state(second)->paused;},3000))return false;
    PostMessageW(a->window,WM_LB_CONTROL,3,200);PostMessageW(b->window,WM_LB_CONTROL,3,700);if(!wait([&]{return llabs(player_result_state(first)->position-200)<80&&llabs(player_result_state(second)->position-700)<80;},4000))return false;
    player_result_stop(first);if(!wait([&]{return player_result_slots()==1;},4000))return false;
    b=player_result_state(second);if(!b||!b->ready||b->pid!=second_pid||llabs(b->position-700)>=80)return false;
    player_result_stop(second);if(!wait([&]{return result_slots.empty();},4000))return false;
    lb::unique_handle ended_first(OpenProcess(SYNCHRONIZE,FALSE,first_pid)),ended_second(OpenProcess(SYNCHRONIZE,FALSE,second_pid));
    return (!ended_first||WaitForSingleObject(ended_first.get(),0)==WAIT_OBJECT_0)&&(!ended_second||WaitForSingleObject(ended_second.get(),0)==WAIT_OBJECT_0)&&g_app.player.position==old_preview&&g_app.wallpaper.position==old_wallpaper&&!IsWindowVisible(owner);
}
