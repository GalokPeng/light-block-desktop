#include "recording.hpp"
#include "media_process.hpp"
#include "timeline.hpp"
#include "annotation.hpp"
#include "frontend.hpp"
#include "recording_ui_api.h"
#include "recording_audio.hpp"
#include "network.h"
#include <commdlg.h>
#include <gdiplus.h>
#include <thread>
#include <regex>
bool player_result_runtime_test(wchar_t const* source);

namespace lb::recording {
namespace {
struct Edits { RECT crop{};std::filesystem::path overlay; };
struct MediaInfo { int width{},height{};int64_t duration{};double fps{24};bool audio{}; };
struct EditSnapshot {RECT crop{};std::vector<capture::Annotation> items;};
struct Session {
    HWND frame{},controls{},host{},timeline{},format{},resolution{},fps{},quality{},save{},copy{},close{},label{},editor{},overlay_window{};
    RECT region{},monitor_bounds{};HMONITOR monitor{};bool display_changed{},controls_excluded{};Timeline range;Settings settings{};
    std::filesystem::path directory,source,preview,destination;
    media::Control control;std::thread thread;
    std::atomic_bool done{};std::atomic<int64_t> progress{},frames{};
    std::shared_ptr<std::atomic_bool> audio_stop{std::make_shared<std::atomic_bool>()};std::atomic_bool audio_merging{};std::wstring audio_error;
    HRESULT error{S_OK};ULONGLONG began{};int phase{},export_format{},export_resolution{},export_fps{},export_quality{};
    bool stopping{},copy_after{},close_requested{},qt_controls{},ui_dragging{},seek_queued{},choice_gif_fast{true},export_gif_fast{true};
    int choice_format{},choice_resolution{},choice_fps{},choice_quality{1};
    ULONGLONG seek_until{};std::wstring message;
    int source_width{},source_height{};bool imported{},preview_fallback{},stop_hotkey{};MediaInfo metadata;
    Edits edits;std::vector<capture::Annotation> annotations;unique_gdi<HBITMAP> snapshot,overlay_bitmap,overlay_preview;bool crop_mode{},crop_drag{},resume_after{};POINT crop_anchor{};RECT crop_selection{},overlay_crop{};SIZE overlay_size{};capture::Tool annotation_tool{capture::Tool::pen};
    std::deque<EditSnapshot> past,future;capture::Document::History editor_history;
    unique_gdi<HBITMAP> poster;bool waiting_player{},restore_position{};
    UiRecordingState last_ui{};bool ui_valid{};
};
std::unique_ptr<Session> session;
std::vector<std::unique_ptr<Session>> results;
Session* find_session(Session* value){if(session.get()==value)return value;for(auto& result:results)if(result.get()==value)return value;return nullptr;}
Session* find_session_id(ULONGLONG began){if(session&&session->began==began)return session.get();for(auto& result:results)if(result->began==began)return result.get();return nullptr;}
void remove_session(Session* value){if(session.get()==value)session.reset();else std::erase_if(results,[&](auto const& result){return result.get()==value;});}
void retain_result(Session& value){if(session.get()==&value)results.push_back(std::move(session));}
size_t export_jobs();
auto export_save_dialog=&GetSaveFileNameW;
auto import_open_dialog=&GetOpenFileNameW;
auto result_player_request=&player_result_request;
ULONGLONG shutdown_generation{};
int px(int value) { return MulDiv(value,int(g_app.dpi),96); }
LRESULT CALLBACK frame_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK controls_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK crop_proc(HWND,UINT,WPARAM,LPARAM);
void position_overlay(Session &state);
void position_controls(Session &state);
void recording_command(Session &state,int action);
void begin_recording(Session &state);
void update_controls(Session &state);
void begin_preview_fallback(Session &state);
void request_play(Session& state);
Player& preview_player(Session const& state){if(auto player=player_result_state(state.host))return *player;static Player empty;return empty;}
bool owns_player(Session const& state) { return state.host&&player_result_state(state.host)!=nullptr; }
void release_stop_hotkey(Session &state) { if(state.stop_hotkey){configure_tool_hotkeys(g_app.settings.hotkeys_enabled!=0,false);state.stop_hotkey=false;} }
void set_status(Session &state,wchar_t const* text) { state.message=text?text:L"";if(state.label) SetWindowTextW(state.label,text); }
bool valid_recording_size(RECT region){auto width=int64_t(region.right)-region.left,height=int64_t(region.bottom)-region.top;return width>=2&&height>=2&&width*height<=32*1024*1024;}
bool monitor_unchanged(HMONITOR monitor,RECT bounds,decltype(&GetMonitorInfoW) query=&GetMonitorInfoW){MONITORINFO current{sizeof current};return monitor&&query(monitor,&current)&&EqualRect(&current.rcMonitor,&bounds);}
void check_recording_display(Session& state){
    if(state.phase!=1||state.stopping||monitor_unchanged(state.monitor,state.monitor_bounds))return;
    state.display_changed=true;state.stopping=true;*state.audio_stop=true;state.control.stop=true;release_stop_hotkey(state);set_status(state,tr(L"显示器布局已变化，正在停止录制并保留已录内容。",L"The display layout changed. Stopping and preserving the recorded content."));
}
bool controls_overlap(RECT region,RECT controls,bool excluded){RECT overlap{};return !excluded&&IntersectRect(&overlap,&region,&controls);}
void protect_recording_controls(Session& state){if(state.phase!=1||!state.controls)return;RECT rect{};GetWindowRect(state.controls,&rect);if(controls_overlap(state.region,rect,state.controls_excluded)){if(state.qt_controls){RECT frame{};GetWindowRect(state.frame,&frame);frontend::recording_ui()->position(uintptr_t(state.controls),frame.left,frame.top,frame.right,frame.bottom,false);}else ShowWindow(state.controls,SW_HIDE);}}
std::wstring media_failure(HRESULT error,std::filesystem::path const& log={}){
    std::wstring message;if(!log.empty()){unique_handle file(CreateFileW(log.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));LARGE_INTEGER size{};if(file&&GetFileSizeEx(file.get(),&size)&&size.QuadPart>0){LARGE_INTEGER offset{};offset.QuadPart=std::max<LONGLONG>(0,size.QuadPart-6144);SetFilePointerEx(file.get(),offset,nullptr,FILE_BEGIN);std::string tail(size_t(std::min<LONGLONG>(size.QuadPart,6144)),0);DWORD read{};if(ReadFile(file.get(),tail.data(),DWORD(tail.size()),&read,nullptr)){tail.resize(read);int count=MultiByteToWideChar(CP_UTF8,0,tail.data(),int(tail.size()),nullptr,0);if(count>0){message.resize(size_t(count));MultiByteToWideChar(CP_UTF8,0,tail.data(),int(tail.size()),message.data(),count);}}}}
    while(!message.empty()&&iswspace(message.back()))message.pop_back();if(message.size()>1800)message.erase(0,message.size()-1800);
    if(message.empty()){wchar_t* text=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,HRESULT_FACILITY(error)==FACILITY_WIN32?HRESULT_CODE(error):DWORD(error),0,reinterpret_cast<wchar_t*>(&text),0,nullptr);if(text){message=text;LocalFree(text);}}
    if(message.empty())message=L"Media processing failed.";return message;
}
UiRecordingState ui_state(Session const& state) {
    UiRecordingState out{};out.phase=state.phase;out.english=g_app.english;out.paused=!owns_player(state)||preview_player(state).paused;out.stopping=state.stopping;out.stop_hotkey=state.stop_hotkey;
    out.editing_crop=state.phase==6&&state.crop_mode;
    out.system_audio=state.settings.record_system_audio;out.microphone=state.settings.record_microphone;out.audio_merging=state.audio_merging;
    out.has_audio=state.metadata.audio;out.preview_muted=!owns_player(state)||preview_player(state).muted;
    out.can_undo=!state.past.empty();out.can_redo=!state.future.empty();
    out.width=state.source_width;out.height=state.source_height;
    out.format=state.choice_format;out.resolution=state.choice_resolution;out.fps=state.choice_fps;out.quality=state.choice_quality;out.gif_fast=state.choice_gif_fast;
    out.elapsed_ms=state.progress.load()/1000;out.duration_ms=state.range.duration;out.source_fps=state.range.fps;
    out.frames=state.range.frames;out.first=state.range.first;out.last=state.range.last;out.position=state.range.position;
    auto duration=state.range.milliseconds(state.range.last)-state.range.milliseconds(state.range.first);
    out.progress_percent=int(std::clamp<int64_t>(state.progress.load()/10/std::max<int64_t>(1,duration),0,99));
    StringCchCopyW(out.status,240,state.message.c_str());return out;
}
void ui_command(void* context,UiRecordingCommand const* command) {
    auto owner=find_session(static_cast<Session*>(context));if(!command||!owner||owner->close_requested)return;auto &state=*owner;
    if(command->action==25||command->action==26){if(state.phase!=0)return;if(!settings_recording_audio(command->action==26,command->value!=0)){set_status(state,tr(L"音频选项未能保存，已保留原选择。",L"Audio preferences could not be saved. The previous choice was kept."));update_controls(state);return;}if(command->action==25)state.settings.record_system_audio=command->value!=0;else state.settings.record_microphone=command->value!=0;state.message.clear();update_controls(state);return;}
    if(command->action>=20&&command->action<=24) {
        if(state.phase!=3) return;
        switch(command->action) {case 20:state.choice_format=std::clamp(command->value,0,2);break;case 21:state.choice_resolution=std::clamp(command->value,0,3);break;case 22:state.choice_fps=std::clamp(command->value,0,5);break;case 23:state.choice_quality=std::clamp(command->value,0,2);break;case 24:state.choice_gif_fast=command->value==0;break;}return;
    }
    if(command->action==13) {
        if(state.phase!=3||!state.range.frames) return;
        state.range.first=std::clamp<int64_t>(command->first,0,state.range.frames-1);state.range.last=std::clamp<int64_t>(command->last,state.range.first+1,state.range.frames);
        state.range.position=std::clamp<int64_t>(command->position,state.range.first,state.range.last-1);state.ui_dragging=command->dragging!=0;state.seek_until=GetTickCount64()+300;
        if(state.seek_queued) return;state.seek_queued=true;
    }
    // Return from Qt input delivery before closing/replacing any toolbar window.
    PostMessageW(state.frame,WM_APP+84,WPARAM(command->action),0);
}
bool create_ui_controls(Session &state) {
    auto api=frontend::recording_ui();if(!api) return false;auto data=ui_state(state);state.controls=HWND(api->create(uintptr_t(state.frame),&data,ui_command,&state));state.qt_controls=state.controls!=nullptr;return state.qt_controls;
}
void update_controls(Session &state) { if(state.qt_controls&&state.controls) { auto data=ui_state(state);if(!state.ui_valid||memcmp(&state.last_ui,&data,sizeof data)){state.last_ui=data;state.ui_valid=true;frontend::recording_ui()->update(uintptr_t(state.controls),&data);}if(state.phase==6)position_controls(state); } }
void destroy_controls(Session &state) { if(state.controls) { if(state.qt_controls) frontend::recording_ui()->destroy(uintptr_t(state.controls));else DestroyWindow(state.controls); }state.controls=state.label=state.timeline=state.save=state.copy=nullptr;state.qt_controls=false; }
std::wstring decimal(double value) { wchar_t text[40];StringCchPrintfW(text,40,L"%.6f",value);return text; }
std::filesystem::path export_temporary(std::filesystem::path destination,wchar_t const* suffix){static std::atomic<ULONGLONG> attempt{1};destination+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(attempt.fetch_add(1))+suffix;return destination;}
std::wstring filters(int resolution,int fps,int quality,bool pad) {
    std::wstring filter;
    if(fps) filter=L"fps="+std::to_wstring(fps)+L",";
    if(resolution) filter+=L"scale=-2:'min(ih,"+std::to_wstring(resolution)+L")':flags="+(quality==2?L"bilinear":L"lanczos")+L",";
    return filter+(pad?L"pad=ceil(iw/2)*2:ceil(ih/2)*2":L"null");
}
HRESULT encode(std::filesystem::path const& source,std::filesystem::path const& destination,Timeline range,int format,int resolution,int fps,int quality,media::Control &control,std::function<void(media::Progress)> progress={},Edits const& edits={},bool gif_fast=true,bool preserve_audio=false,int gif_width=0,bool replace_destination=false,std::wstring* detail=nullptr) {
    if(detail)detail->clear();
    if(format<0||format>2||range.last<=range.first||range.fps<=0) return E_INVALIDARG;
    std::error_code same_error;if(std::filesystem::equivalent(source,destination,same_error)) return E_INVALIDARG;
    if(!replace_destination&&std::filesystem::exists(destination,same_error))return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    auto temporary=export_temporary(destination,L".partial");
    auto palette=temporary;palette+=L".png";auto log=temporary;log+=L".log";
    bool completed=false;scope_exit cleanup([&]{std::error_code ignored;std::filesystem::remove(temporary,ignored);std::filesystem::remove(palette,ignored);if(completed||control.cancel)std::filesystem::remove(log,ignored);});
    auto failure=[&](HRESULT error){if(detail)*detail=media_failure(error,log);return error;};
    // The timeline stores frame boundaries. Millisecond rounding could skip a
    // selected frame at rates such as 24 / 29.97 fps, so seek on microseconds.
    double selected_seconds=double(range.last-range.first)/range.fps;
    auto duration=decimal(std::floor(selected_seconds*1000000.)/1000000.);
    auto inputs=std::vector<std::wstring>{L"-ss",decimal(std::floor(double(range.first)*1000000./range.fps)/1000000.),L"-t",duration,L"-i",source.wstring()};
    bool marked=!edits.overlay.empty();if(marked) inputs.insert(inputs.end(),{L"-loop",L"1",L"-i",edits.overlay.wstring()});
    double output_rate=fps?double(fps):range.fps;
    auto filter=filters(resolution,format==1?0:fps,quality,format==0);
    auto clip=edits.crop;if(clip.right>clip.left&&clip.bottom>clip.top) filter=L"crop="+std::to_wstring(clip.right-clip.left)+L":"+std::to_wstring(clip.bottom-clip.top)+L":"+std::to_wstring(clip.left)+L":"+std::to_wstring(clip.top)+L":exact=1,"+filter;
    if(format==1&&gif_width>0)filter+=L",scale="+std::to_wstring(gif_width)+L":-2:flags=lanczos";
    auto output_filter=format==1?L"fps="+decimal(output_rate)+L":round=up,"+filter:filter;
    std::wstring prefix=marked?L"[0:v][1:v]overlay=0:0:shortest=1,":L"[0:v]";
    int64_t duration_us=std::max<int64_t>(1,std::llround(selected_seconds*1000000.));
    auto report=[&](media::Progress value,bool palette_pass) { if(!progress) return;if(format==1) value.microseconds=(palette_pass?0:duration_us/2)+std::clamp<int64_t>(value.microseconds,0,duration_us)/2;progress(value); };
    if(format==1) {
        // Both modes preserve output pixels and frame rate. Fast mode samples
        // only colour analysis, then applies that palette to full-size frames.
        auto analysis=L"fps="+decimal(gif_fast?std::min(8.,output_rate):output_rate)+L":round=up,"+filter;
        if(gif_fast) analysis+=L",scale=w='min(iw,640)':h=-2:flags=area";
        auto arguments=inputs;arguments.insert(arguments.end(),{L"-filter_complex_threads",L"1",L"-filter_complex",prefix+analysis+L",palettegen=stats_mode=diff",L"-an",L"-frames:v",L"1",L"-update",L"1",L"-f",L"image2",palette.wstring()});
        auto result=media::run(arguments,log,control,[&](media::Progress value){report(value,true);});if(FAILED(result.error)) return failure(result.error);
    }
    auto arguments=inputs;
    if(format==1) arguments.insert(arguments.end(),{L"-i",palette.wstring()});
    arguments.insert(arguments.end(),{L"-t",duration});if(format==1||!preserve_audio) arguments.push_back(L"-an");
    if(range.first||range.last!=range.frames) arguments.insert(arguments.end(),{L"-frames:v",std::to_wstring(std::max<int64_t>(1,int64_t(std::ceil((range.last-range.first)*output_rate/range.fps))))});
    if(format==1) {
        arguments.insert(arguments.end(),{L"-filter_complex_threads",L"1",L"-filter_complex",prefix+output_filter+L"[v];[v]["+(marked?L"2":L"1")+L":v]paletteuse=dither="+(gif_fast?L"bayer:bayer_scale=3":L"sierra2_4a")+L":diff_mode=rectangle"});
        arguments.insert(arguments.end(),{L"-loop",L"0",L"-f",L"gif",temporary.wstring()});
    }else if(format==2) {
        arguments.insert(arguments.end(),{marked?L"-filter_complex":L"-vf",(marked?prefix:L"")+output_filter+(marked?L"[outv]":L""),L"-map",marked?L"[outv]":L"0:v:0",L"-c:v",L"ffv1",L"-level",L"3"});if(preserve_audio) arguments.insert(arguments.end(),{L"-map",L"0:a?",L"-c:a",L"flac"});arguments.insert(arguments.end(),{L"-f",L"matroska",temporary.wstring()});
    }else {
        arguments.insert(arguments.end(),{marked?L"-filter_complex":L"-vf",(marked?prefix:L"")+output_filter+(marked?L"[outv]":L""),L"-map",marked?L"[outv]":L"0:v:0",L"-c:v",L"libx264",L"-preset",quality==2?L"ultrafast":L"veryfast",L"-crf",quality==0?L"16":quality==2?L"24":resolution==720?L"21":resolution==480?L"24":L"18",L"-pix_fmt",L"yuv420p"});if(preserve_audio) arguments.insert(arguments.end(),{L"-map",L"0:a?",L"-c:a",L"aac",L"-b:a",L"192k"});arguments.insert(arguments.end(),{L"-movflags",L"+faststart",L"-f",L"mp4",temporary.wstring()});
    }
    auto result=media::run(arguments,log,control,[&](media::Progress value){report(value,false);});if(FAILED(result.error)) return failure(result.error);if(control.cancel) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    std::error_code error;if(!std::filesystem::is_regular_file(temporary,error)||std::filesystem::file_size(temporary,error)<1||error) return failure(E_FAIL);
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),(replace_destination?MOVEFILE_REPLACE_EXISTING:0)|MOVEFILE_WRITE_THROUGH))return failure(HRESULT_FROM_WIN32(GetLastError()));completed=true;return S_OK;
}
HRESULT probe_media(std::filesystem::path const& source,std::filesystem::path const& log,media::Control &control,MediaInfo &info) {
    // Metadata-only probe: no frame queue, network URL or unbounded decode.
    auto result=media::run({L"-loglevel",L"info",L"-probesize",L"8388608",L"-analyzeduration",L"5000000",L"-i",source.wstring()},log,control);
    if(FAILED(result.error)&&result.exit_code!=1) return result.error;
    std::unique_ptr<wchar_t,decltype(&free)> contents(read_utf8_file(log.c_str(),131072),free);if(!contents) return E_FAIL;std::wstring text=contents.get();std::wsmatch match;
    if(!std::regex_search(text,match,std::wregex(LR"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))"))) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    double seconds_value=std::stod(match[1])*3600.+std::stod(match[2])*60.+std::stod(match[3]);if(!std::isfinite(seconds_value)||seconds_value<=0||seconds_value>86400) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);info.duration=int64_t(std::llround(seconds_value*1000.));
    size_t first=0;bool video=false;while(first<text.size()) { auto end=text.find(L'\n',first);if(end==std::wstring::npos) end=text.size();auto line=text.substr(first,end-first);if(line.find(L"Audio:")!=std::wstring::npos) info.audio=true;if(!video&&line.find(L"Video:")!=std::wstring::npos&&line.find(L"attached pic")==std::wstring::npos) {
        if(std::regex_search(line,match,std::wregex(LR"((?:^|,\s*)([1-9]\d{0,4})x([1-9]\d{0,4})(?=[\s,\[]|$))"))) { info.width=std::stoi(match[1]);info.height=std::stoi(match[2]);video=true; }
        if(std::regex_search(line,match,std::wregex(LR"(([0-9]+(?:\.[0-9]+)?) (?:fps|tbr)\b)"))) info.fps=std::stod(match[1]);
    }first=end+1; }
    if(!video||info.width<2||info.height<2||info.width>16384||info.height>16384||uint64_t(info.width)*info.height>32*1024*1024||!std::isfinite(info.fps)||info.fps<1||info.fps>240) return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);return S_OK;
}
// A studio job owns only its worker and immutable request. It never reuses the
// recording session or hides its controls, so opening the workbench is immediate.
struct Studio {
    UiStudioState view{};MediaInfo info;std::filesystem::path source,destination;
    media::Control control;std::thread worker;std::atomic_bool done{};
    std::atomic<int> progress{};HRESULT error{S_OK};bool probing{};std::wstring detail;
    ~Studio(){control.cancel=true;if(worker.joinable())worker.join();}
};
std::unique_ptr<Studio> studio;
size_t export_jobs(){size_t count=studio&&studio->view.phase==3?1:0;if(session&&(session->phase==4||session->phase==12))++count;for(auto& result:results)if(result->phase==4||result->phase==12)++count;return count;}
void studio_poll(){
    if(!studio||!studio->worker.joinable()||!studio->done)return;
    auto &job=*studio;job.worker.join();job.view.progress=job.progress.load();
    if(FAILED(job.error)){
        job.view.phase=job.probing?5:2;
        auto text=job.error==HRESULT_FROM_WIN32(ERROR_CANCELLED)?tr(L"任务已取消，原素材保持不变。",L"Cancelled. Source preserved."):tr(L"处理未完成，请检查素材、输出位置及录屏组件。",L"Could not complete the job. Check the source, output location and recording runtime.");
        StringCchPrintfW(job.view.message,512,L"%s (0x%08X)",text,unsigned(job.error));if(!job.detail.empty()){auto full=std::wstring(job.view.message)+L"\n"+job.detail.substr(job.detail.size()>360?job.detail.size()-360:0);StringCchCopyW(job.view.message,512,full.c_str());}
    }else if(job.probing){
        job.view.phase=2;job.view.width=job.info.width;job.view.height=job.info.height;job.view.duration_ms=job.info.duration;job.view.fps=job.info.fps;
        StringCchCopyW(job.view.message,512,tr(L"素材已就绪；导出时保留原文件。",L"Media ready. Exports preserve the source."));
    }else {job.view.phase=4;job.view.progress=100;StringCchCopyW(job.view.destination,4096,job.destination.c_str());StringCchCopyW(job.view.message,512,tr(L"导出完成，原素材保持不变。",L"Export complete. Source preserved."));}
    ++job.view.serial;
}
void studio_state(UiStudioState* out){if(!out)return;studio_poll();*out=studio?studio->view:UiStudioState{};if(studio&&(out->phase==1||out->phase==3))out->progress=studio->progress.load();}
int studio_open(wchar_t const* path){
    studio_poll();if(studio&&studio->worker.joinable())return 0;
    try{
        if(!path||!*path||wcslen(path)>=4096)return 0;auto source=std::filesystem::absolute(path).lexically_normal();if(!std::filesystem::is_regular_file(source))return 0;
        auto next=std::make_unique<Studio>();next->view.serial=studio?studio->view.serial+1:1;next->view.phase=1;next->source=source;next->probing=true;next->control.timeout_ms=15000;StringCchCopyW(next->view.source,4096,source.c_str());StringCchCopyW(next->view.message,512,tr(L"正在读取素材…",L"Reading media…"));
        auto &job=*next;auto directory=std::filesystem::path(g_app.data)/L"recordings";std::filesystem::create_directories(directory);auto log=directory/(L"studio-probe-"+std::to_wstring(GetTickCount64())+L".log");
        job.worker=std::thread([&job,log]{try{job.error=probe_media(job.source,log,job.control,job.info);if(FAILED(job.error))job.detail=media_failure(job.error,log);}catch(...){job.error=E_FAIL;}std::error_code ignored;std::filesystem::remove(log,ignored);job.done=true;});studio=std::move(next);return 1;
    }catch(...){return 0;}
}
int studio_export(UiStudioExport const* request){
    studio_poll();if(!request||!studio||studio->worker.joinable()||(studio->view.phase!=2&&studio->view.phase!=4))return 0;
    if(export_jobs()>=2)return 0;
    try{
        auto &job=*studio;auto spec=*request;
        if(spec.first_ms<0||spec.last_ms<=spec.first_ms||spec.last_ms>job.info.duration+1||spec.format<0||spec.format>2||spec.x<0||spec.y<0||spec.width<=0||spec.height<=0||int64_t(spec.x)+spec.width>job.info.width||int64_t(spec.y)+spec.height>job.info.height||spec.fps<1||spec.fps>60||(spec.gif_width!=0&&spec.gif_width!=320&&spec.gif_width!=640&&spec.gif_width!=960)||!spec.destination[0]||wcsnlen(spec.destination,4096)>=4096)return 0;
        auto destination=std::filesystem::absolute(spec.destination).lexically_normal();if(std::filesystem::exists(destination)||!std::filesystem::is_directory(destination.parent_path()))return 0;
        Timeline range;range.reset(job.info.duration,job.info.fps);range.first=std::clamp<int64_t>(std::llround(spec.first_ms*job.info.fps/1000.),0,range.frames-1);range.last=std::clamp<int64_t>(std::llround(spec.last_ms*job.info.fps/1000.),range.first+1,range.frames);
        Edits edits{{spec.x,spec.y,spec.x+spec.width,spec.y+spec.height},{}};job.destination=destination;job.probing=false;job.done=false;job.control.cancel=false;job.control.timeout_ms=0;job.progress=0;job.view.phase=3;job.error=S_OK;job.detail.clear();StringCchCopyW(job.view.message,512,tr(L"正在导出…原素材保持不变。",L"Exporting… Source preserved."));++job.view.serial;
        job.worker=std::thread([&job,range,spec,edits]{try{auto duration=std::max<int64_t>(1,range.milliseconds(range.last)-range.milliseconds(range.first));job.error=encode(job.source,job.destination,range,spec.format,0,spec.format==1?spec.fps:0,1,job.control,[&job,duration](media::Progress p){job.progress=int(std::clamp<int64_t>(p.microseconds/10/duration,0,99));},edits,spec.gif_fast!=0,true,spec.gif_width,false,&job.detail);}catch(...){job.error=E_FAIL;}job.done=true;});return 1;
    }catch(...){if(studio&&!studio->worker.joinable()&&studio->view.phase==3){studio->view.phase=2;++studio->view.serial;StringCchCopyW(studio->view.message,512,tr(L"导出任务未能启动，请稍后重试。",L"Could not start export. Please try again."));}return 0;}
}
void studio_cancel(){if(studio)studio->control.cancel=true;}
bool studio_shutdown(){studio_cancel();studio_poll();if(studio&&studio->worker.joinable())return false;studio.reset();return true;}
void launch_job(Session &state,std::function<void()> action) {
    if(state.thread.joinable()) state.thread.join();state.done=false;state.control.cancel=false;state.control.stop=false;state.progress=0;state.frames=0;state.error=S_OK;state.audio_error.clear();
    state.control.timeout_ms=state.phase==5||state.phase==8||state.phase==12?15000:state.phase==9?180000:0;
    state.thread=std::thread([&state,action=std::move(action)] { try { action(); }catch(...) { state.error=E_FAIL; }state.done=true; });
    SetTimer(g_app.window,18,100,nullptr);
}
void begin_preview_fallback(Session &state) {
    state.preview_fallback=true;state.phase=9;if(owns_player(state)) player_result_stop(state.host);state.preview=state.directory/(L"import-preview-"+std::to_wstring(state.began)+L".mp4");set_status(state,tr(L"正在生成兼容预览；原素材和导出分辨率保持不变。",L"Preparing compatible playback. Original source and export size are unchanged."));auto range=state.range;
    launch_job(state,[&state,range]{state.error=encode(state.source,state.preview,range,0,720,0,2,state.control,[&state](media::Progress progress){state.progress=progress.microseconds;},{},true,state.metadata.audio,0,false,&state.audio_error);});update_controls(state);
}
struct PreviewLayout {RECT crop{},viewport{},host{};};
PreviewLayout preview_layout(int width,int height,RECT bounds,RECT crop){
    PreviewLayout result{};if(width<1||height<1)return result;RECT full{0,0,width,height};if(crop.right<=crop.left||crop.bottom<=crop.top)crop=full;IntersectRect(&result.crop,&crop,&full);if(IsRectEmpty(&result.crop))result.crop=full;
    int cw=result.crop.right-result.crop.left,ch=result.crop.bottom-result.crop.top;result.viewport=fit_rect(bounds,UINT(cw),UINT(ch),FALSE);double scale=double(result.viewport.right-result.viewport.left)/cw;
    int x=result.viewport.left-int(std::llround(result.crop.left*scale)),y=result.viewport.top-int(std::llround(result.crop.top*scale));result.host={x,y,x+LONG(std::llround(width*scale)),y+LONG(std::llround(height*scale))};return result;
}
unique_gdi<HBITMAP> scaled_overlay(HBITMAP source,RECT crop,SIZE size){
    BITMAP original{};if(!source||!GetObjectW(source,sizeof original,&original)||!original.bmBits||size.cx<1||size.cy<1)return {};BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),size.cx,-size.cy,1,32,BI_RGB,0,0,0,0,0};void* data=nullptr;unique_gdi<HBITMAP> result(CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&data,nullptr,0));if(!result)return {};memset(data,0,size_t(size.cx)*size.cy*4);
    Gdiplus::Bitmap target(size.cx,size.cy,size.cx*4,PixelFormat32bppPARGB,static_cast<BYTE*>(data)),image(original.bmWidth,original.bmHeight,original.bmWidthBytes,PixelFormat32bppPARGB,static_cast<BYTE*>(original.bmBits));Gdiplus::Graphics graphics(&target);graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);graphics.DrawImage(&image,Gdiplus::Rect(0,0,size.cx,size.cy),crop.left,crop.top,crop.right-crop.left,crop.bottom-crop.top,Gdiplus::UnitPixel);graphics.Flush(Gdiplus::FlushIntentionSync);return result;
}
void position_overlay(Session &state) {
    if(!state.frame)return;RECT screen{},client{};GetWindowRect(state.frame,&screen);GetClientRect(state.frame,&client);RECT content{2,2,std::max(3L,client.right-2),std::max(3L,client.bottom-2)};auto layout=preview_layout(state.source_width,state.source_height,content,state.edits.crop);
    if(state.editor&&state.crop_mode)SetWindowPos(state.editor,HWND_TOP,2,2,content.right-2,content.bottom-2,SWP_NOACTIVATE);
    if(state.host&&state.phase!=6){RECT current{};GetWindowRect(state.host,&current);MapWindowPoints(nullptr,state.frame,reinterpret_cast<POINT*>(&current),2);if(!EqualRect(&current,&layout.host))MoveWindow(state.host,layout.host.left,layout.host.top,layout.host.right-layout.host.left,layout.host.bottom-layout.host.top,TRUE);}
    if(!state.overlay_window||!state.overlay_bitmap||state.phase==6)return;SIZE size{layout.viewport.right-layout.viewport.left,layout.viewport.bottom-layout.viewport.top};if(!state.overlay_preview||!EqualRect(&state.overlay_crop,&layout.crop)||state.overlay_size.cx!=size.cx||state.overlay_size.cy!=size.cy){state.overlay_preview=scaled_overlay(state.overlay_bitmap.get(),layout.crop,size);state.overlay_crop=layout.crop;state.overlay_size=size;}if(!state.overlay_preview)return;
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)return;auto old=SelectObject(dc,state.overlay_preview.get());POINT origin{screen.left+layout.viewport.left,screen.top+layout.viewport.top},source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(state.overlay_window,nullptr,&origin,&size,dc,&source,0,&blend,ULW_ALPHA);SelectObject(dc,old);DeleteDC(dc);SetWindowPos(state.overlay_window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void end_edit(Session &state) {
    state.phase=3;state.snapshot.reset();state.editor=nullptr;if(state.host)ShowWindow(state.host,SW_SHOWNOACTIVATE);ShowWindow(state.controls,SW_SHOWNOACTIVATE);position_controls(state);set_status(state,tr(L"预览显示当前标注与裁剪；点击播放继续。源文件保留。",L"Preview shows the annotations and crop. Press Play to continue. Source preserved."));
}
size_t edit_bytes(EditSnapshot const& snapshot){size_t bytes=sizeof(snapshot)+snapshot.items.capacity()*sizeof(capture::Annotation);for(auto const& item:snapshot.items)bytes+=item.points.capacity()*sizeof(POINT)+(item.text.capacity()+item.style.font.capacity())*sizeof(wchar_t);return bytes;}
void limit_edit_history(Session& state){
    while(state.past.size()>24)state.past.pop_front();while(state.future.size()>24)state.future.pop_front();size_t bytes=0;for(auto const& item:state.past)bytes+=edit_bytes(item);for(auto const& item:state.future)bytes+=edit_bytes(item);
    while(bytes>8*1024*1024&&(!state.past.empty()||!state.future.empty())){auto& oldest=state.future.empty()||state.past.size()>=state.future.size()?state.past:state.future;bytes-=edit_bytes(oldest.front());oldest.pop_front();}
}
void remember_edit(Session& state){state.past.push_back({state.edits.crop,state.annotations});state.future.clear();limit_edit_history(state);}
void refresh_annotations(Session& state){
    state.overlay_bitmap=capture::overlay(state.source_width,state.source_height,state.annotations);state.overlay_preview.reset();if(state.overlay_window)DestroyWindow(std::exchange(state.overlay_window,nullptr));
    if(!state.annotations.empty()&&state.overlay_bitmap){state.edits.overlay=state.directory/(L"annotations-"+std::to_wstring(state.began)+L".png");state.phase=7;launch_job(state,[&state]{com_apartment apartment(COINIT_MULTITHREADED);if(!save_png(state.overlay_bitmap.get(),state.edits.overlay.c_str(),true))state.error=E_FAIL;});}
    else {state.edits.overlay.clear();state.overlay_bitmap.reset();end_edit(state);}
}
bool step_edit_history(Session& state,bool redo){
    auto& from=redo?state.future:state.past;auto& to=redo?state.past:state.future;if(from.empty())return false;to.push_back({state.edits.crop,state.annotations});auto snapshot=std::move(from.back());from.pop_back();state.edits.crop=snapshot.crop;state.annotations=std::move(snapshot.items);limit_edit_history(state);return true;
}
void history_step(Session& state,bool redo){
    if(state.phase!=3||!step_edit_history(state,redo))return;if(owns_player(state)&&preview_player(state).window)PostMessageW(preview_player(state).window,WM_LB_CONTROL,5,TRUE);refresh_annotations(state);
}
void begin_edit(Session &state,bool crop) {
    if(state.phase!=3||state.editor) return;state.waiting_player=false;state.crop_mode=crop;state.resume_after=owns_player(state)&&!preview_player(state).paused;if(owns_player(state)&&preview_player(state).window) PostMessageW(preview_player(state).window,WM_LB_CONTROL,5,TRUE);ShowWindow(state.overlay_window,SW_HIDE);state.phase=5;EnableWindow(state.save,FALSE);EnableWindow(state.copy,FALSE);set_status(state,tr(L"正在读取当前原始帧…",L"Reading the original frame…"));auto at=decimal(std::floor(double(state.range.position)*1000000./state.range.fps)/1000000.);auto frame=state.directory/(L"edit-frame-"+std::to_wstring(state.began)+L".png");
    launch_job(state,[&state,at,frame] { auto result=media::run({L"-ss",at,L"-i",state.source.wstring(),L"-frames:v",L"1",L"-update",L"1",L"-f",L"image2",frame.wstring()},state.directory/L"edit-frame.log",state.control);state.error=result.error;if(SUCCEEDED(state.error)) { com_apartment apartment(COINIT_MULTITHREADED);state.snapshot.reset(image_thumbnail(frame.c_str(),UINT(state.source_width),UINT(state.source_height)));if(!state.snapshot) state.error=E_FAIL; }DeleteFileW(frame.c_str()); });
}
std::vector<capture::Annotation> editor_items(std::vector<capture::Annotation> items,RECT crop,int width,int height){
    std::erase_if(items,[](auto const& item){return item.tool==capture::Tool::image_crop;});if(crop.right<=crop.left||crop.bottom<=crop.top)crop={0,0,width,height};capture::Annotation marker;marker.tool=capture::Tool::image_crop;marker.points={{crop.left,crop.top},{crop.right,crop.bottom}};items.push_back(std::move(marker));return items;
}
EditSnapshot editor_snapshot(std::vector<capture::Annotation> items,int width,int height){
    RECT full{0,0,width,height},crop{};for(auto it=items.rbegin();it!=items.rend();++it)if(it->tool==capture::Tool::image_crop&&it->points.size()==2){RECT proposed{it->points[0].x,it->points[0].y,it->points[1].x,it->points[1].y},intersection{};if(IntersectRect(&intersection,&full,&proposed)&&intersection.right-intersection.left>=2&&intersection.bottom-intersection.top>=2&&!EqualRect(&intersection,&full))crop=intersection;break;}
    std::erase_if(items,[](auto const& item){return item.tool==capture::Tool::image_crop;});return {crop,std::move(items)};
}
void show_editor(Session &state) {
    RECT rect{};GetWindowRect(state.frame,&rect);RECT location{rect.left+2,rect.top+2,rect.right-2,rect.bottom-2};state.phase=6;if(!state.crop_mode)ShowWindow(state.controls,SW_HIDE);if(state.host)ShowWindow(state.host,SW_HIDE);
    if(state.crop_mode) { state.crop_selection=state.edits.crop;if(state.crop_selection.right<=state.crop_selection.left) state.crop_selection={0,0,state.source_width,state.source_height};state.editor=CreateWindowExW(0,L"LightBlockDesktopNative.VideoCrop",L"",WS_CHILD|WS_VISIBLE,2,2,location.right-location.left,location.bottom-location.top,state.frame,nullptr,g_app.instance,&state);if(state.editor){SetWindowPos(state.editor,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetFocus(state.editor);}else end_edit(state); }
    else { auto id=state.began;capture::Document::History history;for(auto const& item:state.past)history.past.push_back(editor_items(item.items,item.crop,state.source_width,state.source_height));for(auto const& item:state.future)history.future.push_back(editor_items(item.items,item.crop,state.source_width,state.source_height));state.editor_history={};
        auto layout=preview_layout(state.source_width,state.source_height,location,state.edits.crop);location=layout.viewport;
        state.editor=capture::edit_project(state.snapshot.release(),location,editor_items(state.annotations,state.edits.crop,state.source_width,state.source_height),[id](std::vector<capture::Annotation> items) {
        auto owner=find_session_id(id);if(!owner||owner->close_requested)return;auto &s=*owner;s.editor=nullptr;auto snapshot=editor_snapshot(std::move(items),s.source_width,s.source_height);s.annotations=std::move(snapshot.items);s.edits.crop=snapshot.crop;s.past.clear();s.future.clear();for(auto& item:s.editor_history.past)s.past.push_back(editor_snapshot(std::move(item),s.source_width,s.source_height));for(auto& item:s.editor_history.future)s.future.push_back(editor_snapshot(std::move(item),s.source_width,s.source_height));s.editor_history={};limit_edit_history(s);refresh_annotations(s);
        },state.annotation_tool,std::move(history),[id](capture::Document::History history){if(auto owner=find_session_id(id);owner&&!owner->close_requested)owner->editor_history=std::move(history);});if(!state.editor)end_edit(state);
    }
    update_controls(state);position_controls(state);
}
LRESULT CALLBACK crop_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto state=reinterpret_cast<Session*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE) { state=static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(state)); }if(!state) return DefWindowProcW(window,message,wp,lp);
    auto point=[&] { RECT client{};GetClientRect(window,&client);return POINT{MulDiv(std::clamp(GET_X_LPARAM(lp),0,int(client.right)),state->source_width,std::max(1L,client.right)),MulDiv(std::clamp(GET_Y_LPARAM(lp),0,int(client.bottom)),state->source_height,std::max(1L,client.bottom))}; };
    switch(message) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: { PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps),source=CreateCompatibleDC(dc);auto old=SelectObject(source,state->snapshot.get());BITMAP bitmap{};GetObjectW(state->snapshot.get(),sizeof bitmap,&bitmap);RECT client{};GetClientRect(window,&client);StretchBlt(dc,0,0,client.right,client.bottom,source,0,0,bitmap.bmWidth,bitmap.bmHeight,SRCCOPY);SelectObject(source,old);DeleteDC(source);unique_gdi<HPEN> pen(CreatePen(PS_SOLID,2,RGB(112,217,185)));old=SelectObject(dc,pen.get());auto brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));auto r=state->crop_selection;Rectangle(dc,MulDiv(r.left,client.right,state->source_width),MulDiv(r.top,client.bottom,state->source_height),MulDiv(r.right,client.right,state->source_width),MulDiv(r.bottom,client.bottom,state->source_height));SelectObject(dc,old);SelectObject(dc,brush);RECT label{8,8,client.right-8,px(38)};SetBkMode(dc,OPAQUE);SetBkColor(dc,RGB(18,23,32));draw_text(dc,tr(L"拖动框选 · Enter 应用 · Esc 取消 · 右键重选",L"Drag to crop · Enter applies · Esc cancels · Right click resets"),label,g_app.small,RGB(112,217,185),DT_LEFT|DT_VCENTER|DT_SINGLELINE);EndPaint(window,&ps);return 0; }
    case WM_LBUTTONDOWN:state->crop_anchor=point();state->crop_drag=true;SetCapture(window);return 0;
    case WM_MOUSEMOVE:if(state->crop_drag) { auto p=point();state->crop_selection={std::min(state->crop_anchor.x,p.x),std::min(state->crop_anchor.y,p.y),std::max(state->crop_anchor.x,p.x),std::max(state->crop_anchor.y,p.y)};InvalidateRect(window,nullptr,FALSE); }return 0;
    case WM_LBUTTONUP:state->crop_drag=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:state->crop_drag=false;return 0;
    case WM_CANCELMODE:state->crop_drag=false;if(GetCapture()==window)ReleaseCapture();return 0;
    case WM_RBUTTONUP:state->crop_drag=false;if(GetCapture()==window)ReleaseCapture();state->crop_selection={0,0,state->source_width,state->source_height};InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_KEYDOWN:if(wp==VK_RETURN||wp==VK_ESCAPE) { auto rect=state->crop_selection;if(wp==VK_RETURN&&(rect.right-rect.left<2||rect.bottom-rect.top<2)) return 0;if(wp==VK_RETURN){remember_edit(*state);state->edits.crop=rect;}state->crop_drag=false;if(GetCapture()==window)ReleaseCapture();DestroyWindow(window);end_edit(*state);EnableWindow(state->save,TRUE);EnableWindow(state->copy,TRUE); }return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void position_controls(Session &state) {
    if(state.qt_controls&&state.controls) { RECT rect{};GetWindowRect(state.frame,&rect);frontend::recording_ui()->position(uintptr_t(state.controls),rect.left,rect.top,rect.right,rect.bottom,state.phase!=6||state.crop_mode);position_overlay(state);protect_recording_controls(state);return; }
    if(!state.controls) return;RECT rect{};GetWindowRect(state.frame,&rect);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor);int width=std::min(px(800),int(monitor.rcWork.right-monitor.rcWork.left)-16),height=state.phase==1?px(62):px(168);
    int x=std::clamp(int(rect.left),int(monitor.rcWork.left)+8,int(monitor.rcWork.right)-width-8),y=rect.bottom+8;if(y+height>monitor.rcWork.bottom) y=std::max(int(monitor.rcWork.top)+8,int(rect.top)-height-8);
    SetWindowPos(state.controls,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    position_overlay(state);protect_recording_controls(state);
}
HWND button(HWND parent,wchar_t const* text,int id,int x,int y,int width) {
    HWND window=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP,px(x),px(y),px(width),px(31),parent,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);SendMessageW(window,WM_SETFONT,WPARAM(g_app.font),TRUE);return window;
}
HWND choice(HWND parent,int id,int x,int width,std::initializer_list<wchar_t const*> options,int selected) {
    HWND window=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,px(x),px(85),px(width),px(220),parent,reinterpret_cast<HMENU>(INT_PTR(id)),g_app.instance,nullptr);
    for(auto text:options) SendMessageW(window,CB_ADDSTRING,0,LPARAM(text));SendMessageW(window,WM_SETFONT,WPARAM(g_app.small),TRUE);SendMessageW(window,CB_SETCURSEL,selected,0);return window;
}
std::vector<BYTE> clipboard_gif(std::filesystem::path const& path){
    if(_wcsicmp(path.extension().c_str(),L".gif"))return {};unique_handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));LARGE_INTEGER size{};
    if(!file||!GetFileSizeEx(file.get(),&size)||size.QuadPart<6||size.QuadPart>16*1024*1024)return {};std::vector<BYTE> bytes(size_t(size.QuadPart));DWORD read=0;if(!ReadFile(file.get(),bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size()||(memcmp(bytes.data(),"GIF87a",6)&&memcmp(bytes.data(),"GIF89a",6)))return {};return bytes;
}
bool copy_file(std::filesystem::path const& file,HWND owner) {
    size_t bytes=sizeof(DROPFILES)+(file.wstring().size()+2)*sizeof(wchar_t);HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);if(!memory) return false;auto drop=static_cast<DROPFILES*>(GlobalLock(memory));if(!drop) { GlobalFree(memory);return false; }drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;memcpy(reinterpret_cast<BYTE*>(drop)+sizeof(DROPFILES),file.c_str(),file.wstring().size()*sizeof(wchar_t));GlobalUnlock(memory);
    auto gif=clipboard_gif(file);HGLOBAL image=nullptr;if(!gif.empty()){image=GlobalAlloc(GMEM_MOVEABLE,gif.size());if(image){if(auto data=GlobalLock(image)){memcpy(data,gif.data(),gif.size());GlobalUnlock(image);}else{GlobalFree(image);image=nullptr;}}}
    bool copied=false;if(OpenClipboard(owner)) { if(EmptyClipboard()&&SetClipboardData(CF_HDROP,memory)){memory=nullptr;copied=true;if(image&&SetClipboardData(RegisterClipboardFormatW(L"image/gif"),image))image=nullptr;}CloseClipboard(); }if(memory) GlobalFree(memory);if(image)GlobalFree(image);return copied;
}
bool unchanged_export(Session const& state){
    auto extension=state.export_format==2?L".mkv":state.export_format==1?L".gif":L".mp4";
    return !_wcsicmp(state.source.extension().c_str(),extension)&&state.annotations.empty()&&state.edits.overlay.empty()&&state.edits.crop.right<=state.edits.crop.left&&!state.export_resolution&&(!state.export_fps||std::abs(state.export_fps-state.range.fps)<.0001)&&!state.range.first&&state.range.last==state.range.frames;
}
HRESULT copy_original(std::filesystem::path const& source,std::filesystem::path const& destination,media::Control& control){
    std::error_code error;if(std::filesystem::equivalent(source,destination,error)||std::filesystem::exists(destination,error))return HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    auto partial=export_temporary(destination,L".copy-partial");scope_exit cleanup([&]{std::error_code ignored;std::filesystem::remove(partial,ignored);});
    auto progress=[](LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE,LPVOID context)->DWORD{return static_cast<media::Control*>(context)->cancel?PROGRESS_CANCEL:PROGRESS_CONTINUE;};
    if(control.cancel)return HRESULT_FROM_WIN32(ERROR_CANCELLED);if(!CopyFileExW(source.c_str(),partial.c_str(),progress,&control,nullptr,COPY_FILE_FAIL_IF_EXISTS))return HRESULT_FROM_WIN32(GetLastError());if(control.cancel)return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return MoveFileExW(partial.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)?S_OK:HRESULT_FROM_WIN32(GetLastError());
}
void trim_status(Session& state){
    if(!state.range.first&&state.range.last==state.range.frames){set_status(state,tr(L"导出完整录屏",L"Export the complete recording"));return;}wchar_t value[180];StringCchPrintfW(value,180,tr(L"导出片段 %.3f 秒 – %.3f 秒",L"Export range %.3f s – %.3f s"),state.range.first/state.range.fps,state.range.last/state.range.fps);set_status(state,value);
}
bool trim_loop_due(Timeline const& range,int64_t position,bool paused,bool dragging,ULONGLONG now,ULONGLONG seek_until){return !paused&&!dragging&&now>=seek_until&&range.last>range.first&&(position<range.milliseconds(range.first)||position>=range.milliseconds(range.last));}
void park_preview(Session& state){
    if(state.phase!=3||export_jobs()>=2)return;if(owns_player(state)&&preview_player(state).window)PostMessageW(preview_player(state).window,WM_LB_CONTROL,5,TRUE);state.phase=12;auto frame=state.directory/(L"parked-"+std::to_wstring(state.began)+L".png");double scale=std::min({1.,960./state.source_width,540./state.source_height});int width=std::max(2,int(state.source_width*scale)),height=std::max(2,int(state.source_height*scale));auto at=decimal(double(state.range.position)/state.range.fps);
    launch_job(state,[&state,frame,width,height,at]{auto result=media::run({L"-ss",at,L"-i",state.source.wstring(),L"-frames:v",L"1",L"-vf",L"scale="+std::to_wstring(width)+L":"+std::to_wstring(height),L"-update",L"1",L"-f",L"image2",frame.wstring()},state.directory/L"parked-preview.log",state.control);state.error=result.error;if(SUCCEEDED(state.error)){com_apartment apartment(COINIT_MULTITHREADED);state.poster.reset(image_thumbnail(frame.c_str(),UINT(width),UINT(height)));}DeleteFileW(frame.c_str());});
    set_status(state,tr(L"已暂停，正在保留当前画面…",L"Paused. Keeping the current frame…"));
}
void request_play(Session& state){
    std::error_code error;if(!std::filesystem::is_regular_file(state.preview,error)){state.waiting_player=false;set_status(state,tr(L"源文件已移动、删除或不可读取；其他录屏结果保持不变。",L"The source moved, was deleted or is unavailable. Other recording results are unchanged."));return;}
    if(result_player_request(state.preview.c_str(),state.host,state.frame)){state.waiting_player=false;state.restore_position=true;ShowWindow(state.host,SW_SHOWNOACTIVATE);set_status(state,L"");return;}
    if(player_result_slots()<2){state.waiting_player=false;set_status(state,tr(L"播放进程未能启动，请检查素材后重试。",L"Could not start playback. Check the media and try again."));return;}state.waiting_player=true;bool already_parking=std::any_of(results.begin(),results.end(),[](auto const& value){return value->phase==12;});if(!already_parking&&!player_result_releasing())for(auto& other:results)if(other.get()!=&state&&other->phase==3&&owns_player(*other)){park_preview(*other);break;}set_status(state,tr(L"正在准备播放；其他结果和编辑内容会保留。",L"Preparing playback. Other results and edits are retained."));SetTimer(g_app.window,18,100,nullptr);
}
void show_preview(Session &state) {
    retain_result(state);state.phase=3;SetWindowLongPtrW(state.frame,GWL_EXSTYLE,GetWindowLongPtrW(state.frame,GWL_EXSTYLE)&~WS_EX_LAYERED);
    RECT client{};GetClientRect(state.frame,&client);if(!state.host) state.host=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|SS_BLACKRECT,2,2,std::max(1L,client.right-4),std::max(1L,client.bottom-4),state.frame,nullptr,g_app.instance,nullptr);
    request_play(state);
    if(!state.waiting_player&&owns_player(state))state.message.clear();if(state.display_changed)set_status(state,tr(L"显示器布局变化，录制已停止；已录内容保留。",L"Recording stopped after a display layout change. Recorded content is preserved."));
    if(state.qt_controls) update_controls(state);
    else { destroy_controls(state);if(!create_ui_controls(state)) state.controls=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LightBlockDesktopNative.RecordingControls",tr(L"录屏预览与导出",L"Recording preview and export"),WS_POPUP|WS_BORDER,0,0,px(800),px(168),state.frame,nullptr,g_app.instance,&state); }
    position_controls(state);InvalidateRect(state.frame,nullptr,TRUE);
}
void begin_export(Session &state,bool copy) {
    if(state.phase!=3||state.range.last<=state.range.first) return;
    if(export_jobs()>=2){set_status(state,tr(L"已有两个媒体任务正在处理，请稍后导出。",L"Two media jobs are running. Export again when one finishes."));return;}
    state.export_format=state.qt_controls?state.choice_format:int(SendMessageW(state.format,CB_GETCURSEL,0,0));int resolutions[]={0,1080,720,480},rates[]={0,12,15,24,30,60};
    state.export_resolution=resolutions[std::clamp(state.qt_controls?state.choice_resolution:int(SendMessageW(state.resolution,CB_GETCURSEL,0,0)),0,3)];
    state.export_fps=rates[std::clamp(state.qt_controls?state.choice_fps:int(SendMessageW(state.fps,CB_GETCURSEL,0,0)),0,5)];state.export_quality=std::clamp(state.qt_controls?state.choice_quality:int(SendMessageW(state.quality,CB_GETCURSEL,0,0)),0,2);
    state.export_gif_fast=state.choice_gif_fast;
    auto extension=state.export_format==2?L".mkv":state.export_format==1?L".gif":L".mp4";state.copy_after=copy;
    if(copy&&unchanged_export(state)){set_status(state,copy_file(state.source,state.controls)?tr(L"已复制原文件，无需重新编码。",L"Original file copied without re-encoding."):tr(L"剪贴板暂不可用，请重试。",L"The clipboard is unavailable. Try again."));return;}
    if(copy) state.destination=state.directory/(L"export-"+std::to_wstring(GetTickCount64())+extension);
    else {
        auto identity=&state;auto began=state.began;HWND frame=state.frame;
        wchar_t path[LB_PATH];StringCchPrintfW(path,LB_PATH,L"LightBlock-Recording%s",extension);OPENFILENAMEW dialog{sizeof dialog};dialog.hwndOwner=state.controls;dialog.lpstrFile=path;dialog.nMaxFile=LB_PATH;dialog.lpstrFilter=state.export_format==2?L"MKV\0*.mkv\0\0":state.export_format==1?L"GIF\0*.gif\0\0":L"MP4\0*.mp4\0\0";dialog.lpstrDefExt=extension+1;dialog.lpstrInitialDir=g_app.settings.capture_directory[0]?g_app.settings.capture_directory:g_app.data;dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;bool chosen=export_save_dialog(&dialog)!=FALSE;
        // Native dialogs dispatch timers, including asynchronous shutdown. Do
        // not touch state until the owning session has been revalidated.
        auto owner=find_session(identity);if(!chosen||!owner||owner->began!=began||owner->frame!=frame||owner->close_requested||owner->phase!=3)return;
        state.destination=path;if(_wcsicmp(state.destination.extension().c_str(),extension))state.destination.replace_extension(extension);
    }
    std::error_code exists_error;if(std::filesystem::exists(state.destination,exists_error)||exists_error){set_status(state,tr(L"该路径已有文件或不可用，请选择一个新文件名。",L"This path already exists or is unavailable. Choose a new filename."));return;}
    state.waiting_player=false;bool unchanged=unchanged_export(state);if(owns_player(state)&&preview_player(state).window)PostMessageW(preview_player(state).window,WM_LB_CONTROL,5,TRUE);
    auto range=state.range;state.phase=4;EnableWindow(state.save,FALSE);EnableWindow(state.copy,FALSE);set_status(state,tr(L"正在导出；源文件保持不变…",L"Exporting; source preserved…"));
    auto edits=state.edits;launch_job(state,[&state,range,edits,unchanged] { state.error=unchanged?copy_original(state.source,state.destination,state.control):encode(state.source,state.destination,range,state.export_format,state.export_resolution,state.export_fps,state.export_quality,state.control,[&state](media::Progress progress){state.progress=progress.microseconds;},edits,state.export_gif_fast,state.metadata.audio,0,false,&state.audio_error);if(FAILED(state.error)&&state.audio_error.empty())state.audio_error=media_failure(state.error); });
}
void recording_command(Session &state,int action) {
    if(state.close_requested) return;
    auto identity=&state;auto began=state.began;
    switch(action) {
    case 1:if(state.phase==1&&!state.stopping) { release_stop_hotkey(state);*state.audio_stop=true;state.control.stop=true;state.stopping=true;set_status(state,tr(L"正在完成录屏文件…",L"Finalizing recording…")); }break;
    case 27:if(state.phase==0)begin_recording(state);break;
    case 28:if(state.phase==3&&state.metadata.audio&&owns_player(state)&&preview_player(state).window)PostMessageW(preview_player(state).window,WM_LB_CONTROL,2,0);break;
    case 6:if(state.phase==3) { if(owns_player(state)&&preview_player(state).window) PostMessageW(preview_player(state).window,WM_LB_CONTROL,1,0);else request_play(state); }break;
    case 7:begin_export(state,false);break;
    case 8:begin_export(state,true);break;
    case 10:begin_edit(state,false);break;
    case 30:case 31:case 32:if(state.phase==3){state.annotation_tool=action==30?capture::Tool::pen:action==31?capture::Tool::highlight:capture::Tool::eraser;begin_edit(state,false);}break;
    case 35:if(state.phase==3)SetForegroundWindow(state.frame);break;
    case 33:history_step(state,false);break;case 34:history_step(state,true);break;
    case 11:begin_edit(state,true);break;
    case 12:if(state.phase==3) { remember_edit(state);state.annotations.clear();state.edits={};state.overlay_bitmap.reset();state.overlay_preview.reset();if(state.overlay_window) DestroyWindow(std::exchange(state.overlay_window,nullptr));set_status(state,tr(L"已重置标注与画面裁剪，时间裁剪保留。",L"Annotations and crop reset. Time trim is preserved.")); }break;
    case 13:state.seek_queued=false;if(state.phase==3){trim_status(state);if(owns_player(state)&&preview_player(state).window) { if(!preview_player(state).paused) PostMessageW(preview_player(state).window,WM_LB_CONTROL,5,TRUE);PostMessageW(preview_player(state).window,WM_LB_CONTROL,3,LPARAM(state.range.milliseconds(state.range.position))); }else if(!state.ui_dragging){state.waiting_player=false;park_preview(state);} }break;
    case 14:if(state.phase==3) { state.range.first=0;state.range.last=state.range.frames;trim_status(state); }break;
    case 15:case 16:case 17:if(state.phase==6&&state.crop_mode&&IsWindow(state.editor))SendMessageW(state.editor,action==15?WM_RBUTTONUP:WM_KEYDOWN,action==17?VK_RETURN:VK_ESCAPE,0);break;
    case 9:if(state.phase==4) { state.control.cancel=true;set_status(state,tr(L"正在取消导出…",L"Cancelling export…")); }else PostMessageW(state.frame,WM_CLOSE,0,0);break;
    }
    if(auto owner=find_session(identity);owner&&owner->began==began&&!owner->close_requested)update_controls(*owner);
}
LRESULT CALLBACK controls_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto state=reinterpret_cast<Session*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE) { state=static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(state)); }if(!state) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE: {
        state->label=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,px(14),px(state->phase==1?17:126),px(570),px(25),window,nullptr,g_app.instance,nullptr);SendMessageW(state->label,WM_SETFONT,WPARAM(g_app.small),TRUE);
        if(state->phase==1) { button(window,tr(L"结束录屏",L"Stop recording"),1,636,14,146);return 0; }
        state->timeline=timeline_create(window,RECT{px(12),px(7),px(782),px(77)},&state->range,WM_APP+83);
        state->format=choice(window,2,14,90,{L"MP4",L"GIF",L"MKV"},state->choice_format);
        int resolution=0,fps=0;int resolutions[]={0,1080,720,480},rates[]={0,12,15,24,30,60};for(int n=0;n<4;++n) if(resolutions[n]==state->settings.record_resolution) resolution=n;for(int n=0;n<6;++n) if(rates[n]==state->settings.record_fps) fps=n;
        state->resolution=choice(window,3,112,120,{tr(L"原始分辨率",L"Original size"),L"1080p",L"720p",L"480p"},resolution);
        state->fps=choice(window,4,240,105,{tr(L"原始帧率",L"Original FPS"),L"12 fps",L"15 fps",L"24 fps",L"30 fps",L"60 fps"},fps);
        state->quality=choice(window,5,353,116,{tr(L"清晰",L"Best"),tr(L"均衡",L"Balanced"),tr(L"快速",L"Fast")},1);
        button(window,L"▶ / Ⅱ",6,478,84,66);state->save=button(window,tr(L"保存",L"Save"),7,552,84,68);state->copy=button(window,tr(L"复制",L"Copy"),8,628,84,68);state->close=button(window,tr(L"关闭",L"Close"),9,704,84,68);
        button(window,tr(L"标注",L"Draw"),10,578,124,62);button(window,tr(L"画面裁剪",L"Crop"),11,648,124,72);button(window,tr(L"重置",L"Reset"),12,728,124,52);MoveWindow(state->label,px(14),px(126),px(554),px(25),TRUE);
        set_status(*state,tr(L"拖动两端裁剪；← / → 逐帧；Shift 十帧。",L"Drag the two trim handles. Arrows: one frame; Shift: ten."));return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==1) { state->control.stop=true;state->stopping=true;set_status(*state,tr(L"正在完成录屏文件…",L"Finalizing recording…"));EnableWindow(HWND(lp),FALSE); }
        else if(LOWORD(wp)==6&&preview_player(*state).window) PostMessageW(preview_player(*state).window,WM_LB_CONTROL,1,0);
        else if(LOWORD(wp)==7) begin_export(*state,false);else if(LOWORD(wp)==8) begin_export(*state,true);
        else if(LOWORD(wp)==10) begin_edit(*state,false);else if(LOWORD(wp)==11) begin_edit(*state,true);
        else if(LOWORD(wp)==12&&state->phase==3) { state->annotations.clear();state->edits={};state->overlay_bitmap.reset();state->overlay_preview.reset();if(state->overlay_window) DestroyWindow(std::exchange(state->overlay_window,nullptr));set_status(*state,tr(L"已重置标注与画面裁剪，时间裁剪保留。",L"Annotations and crop reset. Time trim is preserved.")); }
        else if(LOWORD(wp)==9) { if(state->phase==4) { state->control.cancel=true;set_status(*state,tr(L"正在取消导出…",L"Cancelling export…")); }else PostMessageW(state->frame,WM_CLOSE,0,0); }return 0;
    case WM_APP+83:if(state->phase==3&&preview_player(*state).window) PostMessageW(preview_player(*state).window,WM_LB_CONTROL,3,LPARAM(state->range.milliseconds(state->range.position)));return 0;
    case WM_ERASEBKGND: { RECT bounds{};GetClientRect(window,&bounds);unique_gdi<HBRUSH> brush(CreateSolidBrush(RGB(28,34,44)));FillRect(HDC(wp),&bounds,brush.get());return 1; }
    case WM_CTLCOLORSTATIC:SetTextColor(HDC(wp),RGB(231,239,247));SetBkColor(HDC(wp),RGB(28,34,44));return LRESULT(GetStockObject(DC_BRUSH));
    case WM_CLOSE:PostMessageW(state->frame,WM_CLOSE,0,0);return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK frame_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto state=reinterpret_cast<Session*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE) { state=static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,LPARAM(state)); }if(!state) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_LB_HELLO:case WM_LB_READY:case WM_LB_ERROR:case WM_LB_POSITION:case WM_LB_DURATION:case WM_LB_STATE:player_message(message,wp,lp);return 0;
    case WM_NCHITTEST:if(state->phase<=1||state->phase==10) return HTTRANSPARENT;return HTCAPTION;
    case WM_PAINT: { PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT bounds{};GetClientRect(window,&bounds);unique_gdi<HBRUSH> brush(CreateSolidBrush(state->phase<=1||state->phase==10?RGB(1,2,3):RGB(15,20,28)));FillRect(dc,&bounds,brush.get());if(state->poster&&!owns_player(*state)){BITMAP bitmap{};GetObjectW(state->poster.get(),sizeof bitmap,&bitmap);auto layout=preview_layout(state->source_width,state->source_height,RECT{2,2,bounds.right-2,bounds.bottom-2},state->edits.crop);HDC source=CreateCompatibleDC(dc);auto old=SelectObject(source,state->poster.get());auto clip=layout.crop;StretchBlt(dc,layout.viewport.left,layout.viewport.top,layout.viewport.right-layout.viewport.left,layout.viewport.bottom-layout.viewport.top,source,MulDiv(clip.left,bitmap.bmWidth,state->source_width),MulDiv(clip.top,bitmap.bmHeight,state->source_height),MulDiv(clip.right-clip.left,bitmap.bmWidth,state->source_width),MulDiv(clip.bottom-clip.top,bitmap.bmHeight,state->source_height),SRCCOPY);SelectObject(source,old);DeleteDC(source);}unique_gdi<HPEN> pen(CreatePen(PS_SOLID,2,state->phase==1?RGB(255,95,103):RGB(123,224,205)));auto p=SelectObject(dc,pen.get()),b=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,0,0,bounds.right,bounds.bottom);SelectObject(dc,p);SelectObject(dc,b);EndPaint(window,&ps);return 0; }
    case WM_DISPLAYCHANGE:check_recording_display(*state);return 0;
    case WM_MOVE:position_controls(*state);return 0;
    case WM_SIZE:if(state->host) MoveWindow(state->host,2,2,std::max(1,int(LOWORD(lp))-4),std::max(1,int(HIWORD(lp))-4),TRUE);position_controls(*state);return 0;
    case WM_CLOSE:state->close_requested=true;*state->audio_stop=true;state->control.cancel=true;SetTimer(g_app.window,18,100,nullptr);return 0;
    case WM_KEYDOWN:{bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;int action=wp==VK_ESCAPE?9:wp==VK_SPACE?6:ctrl&&wp=='S'?7:ctrl&&wp=='C'?8:ctrl&&wp=='Z'?33:ctrl&&wp=='Y'?34:0;if(action)recording_command(*state,action);return 0;}
    case WM_APP+84:recording_command(*state,int(wp));return 0;
    case WM_HOTKEY:if(wp==1&&state->phase==1) recording_command(*state,1);return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void close_windows(Session &state) {
    state.close_requested=true;release_stop_hotkey(state);
    if(state.editor) { DestroyWindow(state.editor);state.editor=nullptr; }if(state.overlay_window) DestroyWindow(std::exchange(state.overlay_window,nullptr));
    if(state.host)player_result_stop(state.host);destroy_controls(state);if(state.frame) DestroyWindow(state.frame);state.frame=state.controls=state.host=nullptr;
}
void register_windows() {
    WNDCLASSW cls{};cls.lpfnWndProc=frame_proc;cls.lpszClassName=L"LightBlockDesktopNative.RecordingFrame";cls.hInstance=g_app.instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);cls.lpfnWndProc=controls_proc;cls.lpszClassName=L"LightBlockDesktopNative.RecordingControls";RegisterClassW(&cls);cls.lpfnWndProc=crop_proc;cls.lpszClassName=L"LightBlockDesktopNative.VideoCrop";RegisterClassW(&cls);
}
void export_choices(Session &state) {
    int resolutions[]={0,1080,720,480},rates[]={0,12,15,24,30,60};for(int n=0;n<4;++n) if(resolutions[n]==state.settings.record_resolution) state.choice_resolution=n;for(int n=0;n<6;++n) if(rates[n]==state.settings.record_fps) state.choice_fps=n;
}
void begin_recording(Session& current){
    if(current.phase!=0)return;if(!monitor_unchanged(current.monitor,current.monitor_bounds)){set_status(current,tr(L"显示器布局已变化，请关闭并重新框选录制区域。",L"The display layout changed. Close this selection and select a recording region again."));update_controls(current);return;}current.phase=1;current.began=GetTickCount64();*current.audio_stop=false;current.audio_merging=false;
    configure_tool_hotkeys(g_app.settings.hotkeys_enabled!=0,true);current.stop_hotkey=tool_hotkey_registered(11);
    int fps=std::clamp(current.settings.record_fps?current.settings.record_fps:24,12,60);
    update_controls(current);protect_recording_controls(current);
    launch_job(current,[&current,fps,english=bool(g_app.english)]{
        bool with_audio=current.settings.record_system_audio||current.settings.record_microphone;
        auto audio_directory=current.directory/(L"audio-"+std::to_wstring(current.began));
        std::unique_ptr<audio::Capture> sound;
        scope_exit clear_started([&]{current.control.started={};current.control.heartbeat={};});
        scope_exit cleanup_audio([&]{if(sound)sound->finish();if((!sound||!sound->abandoned())&&(SUCCEEDED(current.error)||current.control.cancel)){std::error_code ignored;std::filesystem::remove_all(audio_directory,ignored);}});
        if(with_audio){
            std::filesystem::create_directories(audio_directory);sound=std::make_unique<audio::Capture>(audio_directory,current.settings.record_system_audio!=0,current.settings.record_microphone!=0,english,current.audio_stop);
            current.error=sound->prepare();if(FAILED(current.error)){sound->finish();current.audio_error=sound->error_text();return;}
            if(*current.audio_stop||current.control.cancel){current.error=HRESULT_FROM_WIN32(ERROR_CANCELLED);return;}
            current.control.started=[&]{sound->begin();};
            current.control.heartbeat=[&]{if(FAILED(sound->status()))current.control.stop=true;if(current.control.cancel)*current.audio_stop=true;};
        }
        std::vector<std::wstring> args{L"-thread_queue_size",L"8",L"-f",L"gdigrab",L"-draw_mouse",L"1",L"-framerate",std::to_wstring(fps),L"-offset_x",std::to_wstring(current.region.left),L"-offset_y",std::to_wstring(current.region.top),L"-video_size",std::to_wstring(current.region.right-current.region.left)+L"x"+std::to_wstring(current.region.bottom-current.region.top),L"-i",L"desktop",L"-an",L"-threads",std::to_wstring(std::clamp(std::thread::hardware_concurrency()/2,2u,6u)),L"-filter_threads",L"2"};
        if(current.settings.record_lossless)args.insert(args.end(),{L"-c:v",L"ffv1",L"-level",L"3",L"-f",L"matroska"});
        else args.insert(args.end(),{L"-vf",L"pad=ceil(iw/2)*2:ceil(ih/2)*2",L"-c:v",L"libx264",L"-preset",L"ultrafast",L"-crf",L"16",L"-pix_fmt",L"yuv420p",L"-f",L"mp4"});
        args.push_back(current.source.wstring());auto result=media::run(args,current.directory/L"recording.log",current.control,[&current](media::Progress progress){current.progress=progress.microseconds;current.frames=progress.frames;});
        current.error=result.error;if(FAILED(current.error))current.audio_error=media_failure(current.error,current.directory/L"recording.log");current.progress=result.progress.microseconds;current.frames=result.progress.frames;current.control.started={};current.control.heartbeat={};
        if(sound){auto audio_result=sound->finish();if(FAILED(audio_result)){current.error=audio_result;current.audio_error=sound->error_text();return;}}
        if(FAILED(current.error)||current.control.cancel||!sound)return;
        current.audio_merging=true;current.control.stop=false;
        auto merged=current.directory/(L"recording-audio-"+std::to_wstring(current.began)+(current.settings.record_lossless?L".mkv":L".mp4"));
        current.error=audio::mux(current.source,sound->paths(),merged,std::max<int64_t>(1,current.progress.load()),current.settings.record_lossless!=0,current.control);
        if(SUCCEEDED(current.error)){std::error_code ignored;std::filesystem::remove(current.source,ignored);current.source=merged;current.preview=merged;current.metadata.audio=true;}
    });
    protect_recording_controls(current);update_controls(current);InvalidateRect(current.frame,nullptr,FALSE);
}
}
bool active(){return session&&session->phase==1&&!session->stopping;}
bool start(RECT region) {
    if(session) { if(session->controls) SetForegroundWindow(session->controls);return false; }
    if(results.size()>=6){::set_status(L"已有六个录屏结果，请先关闭一个结果再录制。",L"Six recording results are open. Close one before recording again.");return false;}
    if(!valid_recording_size(region)) return false;
    auto state=std::make_unique<Session>();state->region=region;state->monitor=MonitorFromRect(&region,MONITOR_DEFAULTTONULL);MONITORINFO monitor{sizeof monitor};if(!state->monitor||!GetMonitorInfoW(state->monitor,&monitor))return false;state->monitor_bounds=monitor.rcMonitor;state->settings=g_app.settings;state->directory=std::filesystem::path(g_app.data)/L"recordings";if(!ensure_directory(state->directory.c_str())) return false;
    state->phase=0;state->began=GetTickCount64();state->source=state->directory/(L"recording-"+std::to_wstring(state->began)+(state->settings.record_lossless?L".mkv":L".mp4"));state->preview=state->source;
    state->choice_format=state->settings.record_lossless?2:0;state->source_width=region.right-region.left;state->source_height=region.bottom-region.top;export_choices(*state);register_windows();
    session=std::move(state);auto &current=*session;
    current.frame=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"LightBlockDesktopNative.RecordingFrame",tr(L"轻块录屏",L"Light Block Recording"),WS_POPUP|WS_CLIPCHILDREN,region.left-2,region.top-2,region.right-region.left+4,region.bottom-region.top+4,nullptr,nullptr,g_app.instance,&current);
    if(!current.frame) { session.reset();return false; }SetLayeredWindowAttributes(current.frame,RGB(1,2,3),255,LWA_COLORKEY);SetWindowDisplayAffinity(current.frame,0x11);ShowWindow(current.frame,SW_SHOWNOACTIVATE);
    if(!create_ui_controls(current)) current.controls=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LightBlockDesktopNative.RecordingControls",tr(L"录屏控制",L"Recording controls"),WS_POPUP|WS_BORDER,0,0,px(800),px(62),current.frame,nullptr,g_app.instance,&current);current.controls_excluded=SetWindowDisplayAffinity(current.controls,0x11)!=FALSE;position_controls(current);
    // Selection never opens a microphone or captures system audio. Explicit Start does.
    if(!current.qt_controls)begin_recording(current);return true;
}
void toggle(HWND) {
    if(!session) { PostMessageW(g_app.window,WM_COMMAND,713,0);return; }
    if(session->phase==0) { PostMessageW(session->frame,WM_APP+84,27,0);return; }
    if(session->phase==1) { PostMessageW(session->frame,WM_APP+84,1,0);return; }
    if(session->frame) { ShowWindow(session->frame,SW_SHOWNOACTIVATE);SetForegroundWindow(session->frame); }
    if(session->controls) { ShowWindow(session->controls,SW_SHOWNOACTIVATE);SetForegroundWindow(session->controls); }
}
void open_file(HWND owner) {
    if(g_app.close_requested||g_app.player.quitting)return;
    if(session) { if(session->controls) SetForegroundWindow(session->controls);return; }
    if(results.size()>=6){::set_status(L"已有六个媒体结果，请先关闭一个。",L"Six media results are open. Close one first.");return;}
    if(!IsWindowVisible(owner)) { auto foreground=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);owner=pid==GetCurrentProcessId()?foreground:nullptr; }
    auto generation=shutdown_generation;HWND main_window=g_app.window;
    wchar_t path[LB_PATH]{};OPENFILENAMEW dialog{sizeof dialog};dialog.hwndOwner=owner;dialog.lpstrTitle=tr(L"打开视频或 GIF",L"Open video or GIF");dialog.lpstrFilter=L"Video / GIF\0*.mp4;*.mkv;*.mov;*.webm;*.avi;*.m4v;*.gif\0All files\0*.*\0\0";dialog.lpstrFile=path;dialog.nMaxFile=LB_PATH;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_HIDEREADONLY;if(!import_open_dialog(&dialog)) return;
    // A native picker pumps messages: a hotkey can start recording or a quit
    // request can retire this entry while it is open. Never replace its session.
    if(session||results.size()>=6||shutdown_generation!=generation||g_app.close_requested||g_app.player.quitting||g_app.window!=main_window||(main_window&&!IsWindow(main_window)))return;
    auto state=std::make_unique<Session>();state->imported=true;state->phase=8;state->began=GetTickCount64();state->settings=g_app.settings;state->source=path;state->preview=path;state->directory=std::filesystem::path(g_app.data)/L"recordings";if(!ensure_directory(state->directory.c_str())) { error_box(owner,tr(L"无法创建独立媒体缓存目录",L"Cannot create the independent media cache"),HRESULT_FROM_WIN32(GetLastError()));return; }
    auto extension=state->source.extension().wstring();state->choice_format=_wcsicmp(extension.c_str(),L".mkv")==0?2:_wcsicmp(extension.c_str(),L".gif")==0?1:0;export_choices(*state);state->message=tr(L"正在读取媒体信息…",L"Reading media information…");register_windows();
    POINT cursor{};GetCursorPos(&cursor);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&monitor);int width=std::min(px(480),int(monitor.rcWork.right-monitor.rcWork.left)-32),height=std::min(px(270),int(monitor.rcWork.bottom-monitor.rcWork.top)-120);int left=(monitor.rcWork.left+monitor.rcWork.right-width)/2,top=(monitor.rcWork.top+monitor.rcWork.bottom-height)/2;state->region={left,top,left+width,top+height};session=std::move(state);auto &current=*session;
    current.frame=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LightBlockDesktopNative.RecordingFrame",tr(L"轻块桌面 · 媒体预览",L"Light Block · Media preview"),WS_POPUP|WS_CLIPCHILDREN,left-2,top-2,width+4,height+4,nullptr,nullptr,g_app.instance,&current);if(!current.frame) { session.reset();return; }ShowWindow(current.frame,SW_SHOWNOACTIVATE);
    if(!create_ui_controls(current)) current.controls=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"LightBlockDesktopNative.RecordingControls",tr(L"媒体预览",L"Media preview"),WS_POPUP|WS_BORDER,0,0,px(800),px(168),current.frame,nullptr,g_app.instance,&current);position_controls(current);
    launch_job(current,[&current]{current.error=probe_media(current.source,current.directory/(L"probe-"+std::to_wstring(current.began)+L".log"),current.control,current.metadata);if(FAILED(current.error))current.audio_error=media_failure(current.error,current.directory/(L"probe-"+std::to_wstring(current.began)+L".log"));});
}
void tick_one(Session& state) {
    auto identity=&state;scope_exit refresh([identity]{if(auto owner=find_session(identity))update_controls(*owner);});
    if(state.close_requested) { if(state.thread.joinable()&&!state.done) return;if(state.thread.joinable()) state.thread.join();close_windows(state);remove_session(&state);return; }
    if(state.phase==1&&!state.done) { check_recording_display(state);if(state.audio_merging){state.phase=10;set_status(state,tr(L"正在合并音轨，画面无需重新编码…",L"Combining audio without re-encoding video…"));position_controls(state);return;}if(GetTickCount64()-state.began>2*60*60*1000){*state.audio_stop=true;state.control.stop=true;}wchar_t label[180];if(!state.stopping) { auto input=state.settings.record_system_audio?(state.settings.record_microphone?tr(L"系统 + 麦克风",L"System + microphone"):tr(L"系统声音",L"System audio")):(state.settings.record_microphone?tr(L"麦克风",L"Microphone"):tr(L"静音",L"No audio"));StringCchPrintfW(label,180,tr(L"● 录制中 %.1f 秒 · %s",L"● Recording %.1f s · %s"),state.progress.load()/1000000.,input);set_status(state,label); }return; }
    if(state.phase==3) { if(state.waiting_player){request_play(state);if(state.waiting_player)return;}if(state.restore_position&&owns_player(state)&&preview_player(state).ready){state.restore_position=false;PostMessageW(preview_player(state).window,WM_LB_CONTROL,3,LPARAM(state.range.milliseconds(state.range.position)));state.seek_until=GetTickCount64()+300;}if(state.imported&&owns_player(state)&&preview_player(state).failed&&!state.preview_fallback) { begin_preview_fallback(state);return; }if(owns_player(state)&&preview_player(state).ready) { if(trim_loop_due(state.range,preview_player(state).position,preview_player(state).paused!=0,state.ui_dragging,GetTickCount64(),state.seek_until)){state.range.position=state.range.first;state.seek_until=GetTickCount64()+300;PostMessageW(preview_player(state).window,WM_LB_CONTROL,3,LPARAM(state.range.milliseconds(state.range.first)));return;}if(preview_player(state).duration>0&&llabs(preview_player(state).duration-state.range.duration)>100) { auto first=state.range.first,last=state.range.last;bool full=last==state.range.frames;state.range.reset(preview_player(state).duration,state.range.fps);state.range.first=std::min(first,std::max<int64_t>(0,state.range.frames-1));state.range.last=full?state.range.frames:std::clamp(last,state.range.first+1,state.range.frames); }int64_t frame=std::clamp<int64_t>(std::llround(preview_player(state).position*state.range.fps/1000),state.range.first,std::max(state.range.first,state.range.last-1));if(frame!=state.range.position&&!state.ui_dragging&&GetTickCount64()>=state.seek_until&&(!state.timeline||GetCapture()!=state.timeline)) { state.range.position=frame;if(state.timeline) InvalidateRect(state.timeline,nullptr,FALSE); } }return; }
    if(!state.done) return;if(state.thread.joinable()) state.thread.join();
    if(state.phase==12){player_result_stop(state.host);ShowWindow(state.host,SW_HIDE);state.phase=3;set_status(state,SUCCEEDED(state.error)?tr(L"预览已暂停，点击播放继续；编辑与导出仍可用。",L"Preview paused. Press Play to resume; editing and export remain available."):tr(L"预览已暂停，当前帧未能载入；点击播放可重试。",L"Preview paused, but its frame could not load. Press Play to retry."));InvalidateRect(state.frame,nullptr,FALSE);return;}
    if(FAILED(state.error)) { if(state.error!=HRESULT_FROM_WIN32(ERROR_CANCELLED)) error_box(state.controls,state.audio_error.empty()?tr(L"录制或导出失败；源文件保留在独立数据目录",L"Recording/export failed. Source files remain in this app's independent data folder."):state.audio_error.c_str(),state.error);if(state.phase==4||state.phase==5||state.phase==7) { if(state.phase!=4) { state.edits.overlay.clear();end_edit(state); }state.phase=3;EnableWindow(state.save,TRUE);EnableWindow(state.copy,TRUE);set_status(state,state.audio_error.empty()?tr(L"操作未完成；源文件保持不变。",L"Operation failed. Source preserved."):state.audio_error.c_str()); }else { close_windows(state);remove_session(&state); }return; }
    if(state.phase==1||state.phase==10) {
        release_stop_hotkey(state);
        auto duration=std::max<int64_t>(1,state.progress.load()/1000);double fps=state.settings.record_fps?state.settings.record_fps:24;state.range.reset(duration,fps);
        if(state.settings.record_lossless) {
            state.phase=2;state.preview=state.directory/(L"preview-"+std::to_wstring(state.began)+L".mp4");set_status(state,tr(L"正在生成兼容预览，保留无损源文件…",L"Preparing playback; lossless source is preserved…"));EnableWindow(GetDlgItem(state.controls,1),FALSE);position_controls(state);
            auto range=state.range;launch_job(state,[&state,range]{state.error=encode(state.source,state.preview,range,0,720,0,2,state.control,{}, {},true,state.metadata.audio,0,false,&state.audio_error);});
        }else show_preview(state);
    }else if(state.phase==2||state.phase==9) show_preview(state);
    else if(state.phase==8) {
        state.source_width=state.metadata.width;state.source_height=state.metadata.height;state.range.reset(state.metadata.duration,state.metadata.fps);MONITORINFO monitor{sizeof monitor};GetMonitorInfoW(MonitorFromWindow(state.frame,MONITOR_DEFAULTTONEAREST),&monitor);int max_width=std::max(64,int(monitor.rcWork.right-monitor.rcWork.left)-px(48)),max_height=std::max(64,int(monitor.rcWork.bottom-monitor.rcWork.top)-px(220));double scale=std::min({1.,double(max_width)/state.source_width,double(max_height)/state.source_height});int width=std::max(8,int(state.source_width*scale)),height=std::max(8,int(state.source_height*scale));int left=(monitor.rcWork.left+monitor.rcWork.right-width)/2,top=monitor.rcWork.top+std::max(px(12),(max_height-height)/2);state.region={left,top,left+width,top+height};SetWindowPos(state.frame,HWND_TOPMOST,left-2,top-2,width+4,height+4,SWP_NOACTIVATE);if(_wcsicmp(state.source.extension().c_str(),L".gif")==0) begin_preview_fallback(state);else show_preview(state);
    }
    else if(state.phase==5) show_editor(state);
    else if(state.phase==7) { state.overlay_window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"STATIC",L"",WS_POPUP,0,0,1,1,state.frame,nullptr,g_app.instance,nullptr);end_edit(state);EnableWindow(state.save,TRUE);EnableWindow(state.copy,TRUE); }
    else if(state.phase==4) {
        state.phase=3;EnableWindow(state.save,TRUE);EnableWindow(state.copy,TRUE);
        if(state.copy_after){set_status(state,copy_file(state.destination,state.controls)?tr(L"已复制导出文件；源文件与裁剪选区已保留。",L"Exported file copied. Source and trim range preserved."):tr(L"文件已导出，但剪贴板暂不可用，请重试。",L"File exported, but the clipboard is unavailable. Try again."));}
        else {auto previous=std::wstring(g_app.settings.capture_directory);StringCchCopyW(g_app.settings.capture_directory,LB_PATH,state.destination.parent_path().c_str());bool saved=save_config()!=FALSE;if(!saved)StringCchCopyW(g_app.settings.capture_directory,LB_PATH,previous.c_str());set_status(state,((saved?tr(L"已保存到 ",L"Saved to "):tr(L"文件已保存，但默认目录未能记住：",L"File saved, but the default directory could not be remembered: "))+state.destination.wstring()).c_str());}
    }
}
void tick(){
    static bool ticking=false;if(ticking)return;ticking=true;scope_exit leave([]{ticking=false;});std::vector<Session*> pending;if(session)pending.push_back(session.get());for(auto& result:results)pending.push_back(result.get());for(auto candidate:pending)if(auto owner=find_session(candidate))tick_one(*owner);if(!session&&results.empty())KillTimer(g_app.window,18);
}
UiStudioApi const* studio_api(){static UiStudioApi api{LB_STUDIO_UI_ABI,sizeof(UiStudioApi),studio_state,studio_open,studio_export,studio_cancel};return &api;}
bool shutdown() {
    ++shutdown_generation;
    bool studio_ready=studio_shutdown();auto cancel=[](Session& state){state.close_requested=true;*state.audio_stop=true;state.control.cancel=true;};if(session)cancel(*session);for(auto& result:results)cancel(*result);tick();return !session&&results.empty()&&studio_ready;
}
namespace {
int dialog_fixture_mode{};
std::atomic_bool import_fixture_release{};
BOOL WINAPI reentered_import_dialog(OPENFILENAMEW* dialog){
    StringCchCopyW(dialog->lpstrFile,dialog->nMaxFile,L"synthetic-import.mp4");
    if(dialog_fixture_mode==0){session=std::make_unique<Session>();session->began=800;session->source=L"active-recording.mp4";session->thread=std::thread([]{while(!import_fixture_release.load())Sleep(1);});}
    else if(dialog_fixture_mode==1)shutdown();
    else if(dialog_fixture_mode==2){for(int n=0;n<6;++n)results.push_back(std::make_unique<Session>());}
    else g_app.close_requested=TRUE;return TRUE;
}
BOOL WINAPI retired_export_dialog(OPENFILENAMEW*) {
    if(dialog_fixture_mode==2){session->close_requested=true;return TRUE;}
    auto began=session->began;close_windows(*session);session.reset();
    if(dialog_fixture_mode==1){session=std::make_unique<Session>();session->began=began+1;session->phase=3;}
    return TRUE;
}
}
bool dialog_tests(){
    if(session)return false;auto old=export_save_dialog;scope_exit restore([&]{export_save_dialog=old;if(session){close_windows(*session);session.reset();}});export_save_dialog=retired_export_dialog;
    for(int mode=0;mode<3;++mode){session=std::make_unique<Session>();session->phase=3;session->qt_controls=true;session->began=ULONGLONG(100+mode);session->range.reset(1000,24);session->frame=CreateWindowExW(0,L"STATIC",L"hidden export dialog fixture",WS_POPUP,0,0,80,60,nullptr,nullptr,g_app.instance,nullptr);if(!session->frame)return false;dialog_fixture_mode=mode;recording_command(*session,7);
        if(mode==0){if(session)return false;}else {if(!session||session->phase!=3||session->thread.joinable()||!session->destination.empty()||(mode==1&&session->began!=102)||(mode==2&&!session->close_requested))return false;close_windows(*session);session.reset();}
    }
    auto original_open=import_open_dialog;auto original_generation=shutdown_generation;BOOL closing=g_app.close_requested;scope_exit restore_import([&]{import_fixture_release=true;if(session&&session->thread.joinable())session->thread.join();session.reset();results.clear();import_open_dialog=original_open;shutdown_generation=original_generation;g_app.close_requested=closing;});import_open_dialog=reentered_import_dialog;
    for(int mode=0;mode<4;++mode){dialog_fixture_mode=mode;import_fixture_release=false;g_app.close_requested=FALSE;open_file(nullptr);if(mode==0){if(!session||session->began!=800||session->source!=L"active-recording.mp4"||!session->thread.joinable())return false;import_fixture_release=true;session->thread.join();session.reset();}else if(session||((mode==2)!=(results.size()==6)))return false;results.clear();}
    return true;
}
bool crop_controls_tests(){
    if(session)return false;WNDCLASSW cls{};cls.hInstance=g_app.instance;cls.lpfnWndProc=crop_proc;cls.lpszClassName=L"LightBlockDesktopNative.VideoCrop";RegisterClassW(&cls);
    session=std::make_unique<Session>();session->began=GetTickCount64();session->crop_mode=true;session->source_width=800;session->source_height=450;session->region={0,0,800,450};session->frame=CreateWindowExW(0,L"STATIC",L"hidden crop control fixture",WS_POPUP,0,0,804,454,nullptr,nullptr,g_app.instance,nullptr);if(!session->frame){session.reset();return false;}
    scope_exit cleanup([&]{if(session){if(IsWindow(session->editor))DestroyWindow(session->editor);DestroyWindow(session->frame);session.reset();}});
    for(int n=0;n<12;++n){session->snapshot= capture::overlay(800,450,{});show_editor(*session);if(session->phase!=6||!IsWindow(session->editor)||!ui_state(*session).editing_crop||IsWindowVisible(session->frame))return false;auto editor=session->editor;SendMessageW(editor,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));SendMessageW(editor,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(300,180));recording_command(*session,15);if(session->crop_drag||session->crop_selection.right!=800||session->crop_selection.bottom!=450||session->phase!=6||!IsWindow(editor))return false;
        session->crop_selection={20,30,400,250};recording_command(*session,17);RECT expected{20,30,400,250};if(session->phase!=3||session->editor||IsWindow(editor)||!EqualRect(&session->edits.crop,&expected))return false;
        session->snapshot=capture::overlay(800,450,{});show_editor(*session);session->crop_selection={0,0,600,400};recording_command(*session,16);if(session->phase!=3||session->editor||!EqualRect(&session->edits.crop,&expected))return false;
    }return true;
}
bool parity_tests(){
    for(int size:{2,7,8})if(!valid_recording_size({0,0,size,size}))return false;if(valid_recording_size({0,0,1,10})||valid_recording_size({0,0,10,1})||valid_recording_size({0,0,16384,16384}))return false;
    auto monitor_query=+[](HMONITOR monitor,LPMONITORINFO info)->BOOL{if(monitor!=HMONITOR(1))return FALSE;info->rcMonitor={-1920,0,0,1080};return TRUE;};
    if(!monitor_unchanged(HMONITOR(1),{-1920,0,0,1080},monitor_query)||monitor_unchanged(HMONITOR(2),{-1920,0,0,1080},monitor_query)||monitor_unchanged(HMONITOR(1),{-1920,0,0,1200},monitor_query))return false;
    Session changed;changed.phase=1;changed.monitor=HMONITOR(1);changed.monitor_bounds={-1920,0,0,1080};check_recording_display(changed);if(!changed.display_changed||!changed.stopping||!changed.control.stop||!*changed.audio_stop||changed.close_requested)return false;
    if(!controls_overlap({0,0,1920,1080},{10,1020,500,1080},false)||controls_overlap({0,0,1920,1080},{10,1020,500,1080},true)||controls_overlap({0,0,1920,1000},{10,1020,500,1080},false))return false;
    Timeline range;range.reset(10000,24);range.first=24;range.last=48;
    if(!trim_loop_due(range,2000,false,false,1000,0)||!trim_loop_due(range,999,false,false,1000,0)||trim_loop_due(range,1500,false,false,1000,0)||trim_loop_due(range,2000,true,false,1000,0)||trim_loop_due(range,2000,false,true,1000,0)||trim_loop_due(range,2000,false,false,1000,1200))return false;
    auto layout=preview_layout(1920,1080,{2,2,962,542},{480,270,1440,810});RECT host{-478,-268,1442,812},viewport{2,2,962,542};if(!EqualRect(&layout.host,&host)||!EqualRect(&layout.viewport,&viewport))return false;
    auto portrait=preview_layout(1920,1080,{2,2,962,542},{900,40,1020,1040});if(portrait.viewport.left<400||portrait.viewport.right>600||portrait.viewport.top!=2||portrait.viewport.bottom!=542)return false;
    Session state;state.source=L"example.mp4";state.range.reset(1000,24);state.export_fps=24;if(!unchanged_export(state))return false;state.export_resolution=720;if(unchanged_export(state))return false;state.export_resolution=0;state.range.first=1;if(unchanged_export(state))return false;state.range.first=0;
    capture::Annotation mark;mark.tool=capture::Tool::rectangle;mark.style.filled=true;mark.style.color=RGB(255,0,0);mark.points={{10,10},{30,30}};remember_edit(state);state.annotations={mark};remember_edit(state);state.edits.crop={10,10,30,30};if(unchanged_export(state))return false;
    if(!step_edit_history(state,false)||state.edits.crop.right||state.annotations.size()!=1||!step_edit_history(state,false)||!state.annotations.empty()||!step_edit_history(state,true)||state.annotations.size()!=1||!step_edit_history(state,true)||state.edits.crop.right!=30)return false;
    capture::Document reopened(nullptr);reopened.width=reopened.height=40;reopened.items=editor_items(state.annotations,state.edits.crop,40,40);capture::Document::History history;for(auto const& item:state.past)history.past.push_back(editor_items(item.items,item.crop,40,40));reopened.restore_history(std::move(history));
    if(!reopened.undo())return false;auto untrimmed=editor_snapshot(reopened.items,40,40);if(untrimmed.crop.right||untrimmed.items!=state.annotations||!reopened.undo()||!editor_snapshot(reopened.items,40,40).items.empty()||!reopened.redo()||!reopened.redo())return false;auto restored=editor_snapshot(reopened.items,40,40);if(!EqualRect(&restored.crop,&state.edits.crop)||restored.items!=state.annotations)return false;
    auto original=capture::overlay(40,40,{mark});auto preview=scaled_overlay(original.get(),{10,10,30,30},{10,10});BITMAP bitmap{};if(!preview||!GetObjectW(preview.get(),sizeof bitmap,&bitmap)||!bitmap.bmBits)return false;auto pixel=static_cast<uint32_t*>(bitmap.bmBits)[55];if((pixel>>24)<76||(pixel>>24)>84||((pixel>>16)&255)<76||((pixel>>16)&255)>84||(pixel&0xffff)>0x1010)return false;
    Session bounded;capture::Annotation large;large.points.resize(65536);bounded.annotations={std::move(large)};for(int n=0;n<24;++n){bounded.annotations[0].style.number=n;remember_edit(bounded);}size_t bytes=0;for(auto const& item:bounded.past)bytes+=edit_bytes(item);if(bytes>8*1024*1024||bounded.past.empty()||bounded.past.size()>=24||bounded.past.back().items[0].style.number!=23)return false;
    return true;
}
bool multi_result_tests(){
    if(session||!results.empty()||studio)return false;scope_exit cleanup([]{if(session){close_windows(*session);session.reset();}for(auto& result:results)close_windows(*result);results.clear();});
    for(int n=0;n<2;++n){session=std::make_unique<Session>();session->began=ULONGLONG(700+n);session->phase=3;session->range.reset(1000,24);session->source=n?L"second.mp4":L"first.mp4";auto pointer=session.get();retain_result(*pointer);if(session||find_session(pointer)!=pointer||find_session_id(ULONGLONG(700+n))!=pointer)return false;}
    session=std::make_unique<Session>();session->phase=0;session->began=702;session->source=L"third.mp4";auto first=results[0].get(),second=results[1].get();UiRecordingCommand choose{20,1,0,0,0,0};ui_command(first,&choose);if(first->choice_format!=1||second->choice_format||session->choice_format)return false;
    static unsigned player_requests{};player_requests=0;auto original_request=result_player_request;scope_exit restore_request([&]{result_player_request=original_request;});result_player_request=+[](wchar_t const*,HWND,HWND)->BOOL{++player_requests;return FALSE;};Session missing;missing.phase=3;missing.waiting_player=true;missing.preview=L"missing-result-preview.mp4";request_play(missing);if(player_requests||missing.waiting_player||first->phase!=3||second->phase!=3||missing.message.empty())return false;
    first->phase=second->phase=4;if(export_jobs()!=2)return false;first->phase=3;if(export_jobs()!=1)return false;remove_session(first);if(find_session(first)||find_session(second)!=second||results.size()!=1||!session)return false;
    second->close_requested=true;tick();if(!results.empty()||!session||session->phase!=0)return false;return true;
}
bool export_test(std::wstring const& directory) {
    std::filesystem::path root(directory);if(!ensure_directory(root.c_str())) { log_line(L"recording regression failed at line %d",__LINE__);return false; }auto source=root/L"source.mp4";media::Control control;
    auto result=media::run({L"-f",L"lavfi",L"-i",L"testsrc2=size=800x450:rate=24",L"-t",L"1",L"-an",L"-c:v",L"libx264",L"-preset",L"ultrafast",L"-pix_fmt",L"yuv420p",L"-f",L"mp4",source.wstring()},root/L"source.log",control);if(FAILED(result.error)||result.progress.frames<20) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    MediaInfo metadata;if(FAILED(probe_media(source,root/L"source-probe.log",control,metadata))||metadata.width!=800||metadata.height!=450||metadata.duration!=1000||metadata.fps!=24||metadata.audio) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    wchar_t original_hash[65]{};if(!hash_file(source.c_str(),L"SHA256",original_hash,65)) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    auto copied=root/L"original-copy.mp4";if(FAILED(copy_original(source,copied,control))){ log_line(L"recording regression failed at line %d",__LINE__);return false; }wchar_t copy_hash[65]{};if(!hash_file(copied.c_str(),L"SHA256",copy_hash,65)||wcscmp(original_hash,copy_hash)||SUCCEEDED(copy_original(source,copied,control))){ log_line(L"recording regression failed at line %d",__LINE__);return false; }control.cancel=true;if(SUCCEEDED(copy_original(source,root/L"cancelled-copy.mp4",control))||std::filesystem::exists(root/L"cancelled-copy.mp4")){ log_line(L"recording regression failed at line %d",__LINE__);return false; }control.cancel=false;
    Timeline range;range.reset(1000,24);range.first=6;range.last=18;
    if(FAILED(encode(source,root/L"trimmed.mp4",range,0,0,24,1,control))||FAILED(encode(source,root/L"fast.gif",range,1,0,12,1,control,{}, {},true))||FAILED(encode(source,root/L"best.gif",range,1,0,12,1,control,{}, {},false))||FAILED(encode(source,root/L"lossless.mkv",range,2,0,0,1,control))) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    for(auto name:{L"fast.gif",L"best.gif"}) { Image image;if(FAILED(image_open(&image,(root/name).c_str()))||image.width!=800||image.height!=450||image.count!=6) { log_line(L"recording regression failed at line %d",__LINE__);return false; } }
    auto gif_data=clipboard_gif(root/L"fast.gif");if(gif_data.size()<6||memcmp(gif_data.data(),"GIF89a",6)||!clipboard_gif(source).empty()){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    // Odd crop dimensions preserve pixels: H.264 pads only the right/bottom;
    // FFV1 and GIF retain the exact selected geometry. A 2x2 crop is legal.
    for(int format:{0,1,2}){Edits odd{{10,10,329,189},{}};auto file=root/(format==0?L"odd.mp4":format==1?L"odd.gif":L"odd.mkv");if(FAILED(encode(source,file,range,format,0,24,1,control,{},odd))){ log_line(L"recording regression failed at line %d",__LINE__);return false; }MediaInfo info;if(FAILED(probe_media(file,root/L"odd-probe.log",control,info))||info.width!=(format==0?320:319)||info.height!=(format==0?180:179)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }}
    Edits tiny{{0,0,2,2},{}};auto tiny_file=root/L"tiny.mp4";if(FAILED(encode(source,tiny_file,range,0,0,24,1,control,{},tiny))){ log_line(L"recording regression failed at line %d",__LINE__);return false; }MediaInfo tiny_info;if(FAILED(probe_media(tiny_file,root/L"tiny-probe.log",control,tiny_info))||tiny_info.width!=2||tiny_info.height!=2){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    std::wstring detail;auto failed_output=root/L"bad-source.mp4";if(SUCCEEDED(encode(root/L"missing-input.mp4",failed_output,range,0,0,24,1,control,{}, {},true,false,0,false,&detail))||detail.find(L"missing-input.mp4")==std::wstring::npos||std::filesystem::exists(failed_output)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    bool retained_log=false;for(auto const& entry:std::filesystem::directory_iterator(root)){auto name=entry.path().filename().wstring();if(name.starts_with(L"bad-source.mp4.")&&name.ends_with(L".partial.log"))retained_log=true;}if(!retained_log||media_failure(E_ACCESSDENIED).empty()){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    auto race_output=root/L"same-target.mp4";media::Control race_a,race_b;std::atomic_bool go{};std::atomic_int waiting{};HRESULT race_first=E_FAIL,race_second=E_FAIL;
    auto racing=[&](HRESULT& outcome,media::Control& task){++waiting;while(!go.load())Sleep(1);outcome=encode(source,race_output,range,0,0,24,1,task,{},tiny);};
    std::thread first_job([&]{racing(race_first,race_a);}),second_job([&]{racing(race_second,race_b);});while(waiting.load()!=2)Sleep(1);go=true;first_job.join();second_job.join();if(SUCCEEDED(race_first)==SUCCEEDED(race_second)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    MediaInfo raced;if(FAILED(probe_media(race_output,root/L"same-target-probe.log",control,raced))||raced.width!=2||raced.height!=2){ log_line(L"recording regression failed at line %d",__LINE__);return false; }for(auto const& entry:std::filesystem::directory_iterator(root)){auto name=entry.path().filename().wstring();if(name.starts_with(L"same-target.mp4.")&&name.ends_with(L".partial")){ log_line(L"recording regression failed at line %d",__LINE__);return false; }}
    // Compare every decoded frame; FFV1 must preserve the chosen source pixels.
    result=media::run({L"-ss",L"0.250000",L"-t",L"0.500000",L"-i",source.wstring(),L"-an",L"-pix_fmt",L"rgb24",L"-f",L"image2",(root/L"source-frame-%02d.png").wstring()},root/L"source-frames.log",control);if(FAILED(result.error)||result.progress.frames!=12) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    result=media::run({L"-i",(root/L"lossless.mkv").wstring(),L"-an",L"-pix_fmt",L"rgb24",L"-f",L"image2",(root/L"lossless-frame-%02d.png").wstring()},root/L"lossless-frames.log",control);if(FAILED(result.error)||result.progress.frames!=12) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    for(int n=1;n<=12;++n) { wchar_t original[64],lossless[64];StringCchPrintfW(original,64,L"source-frame-%02d.png",n);StringCchPrintfW(lossless,64,L"lossless-frame-%02d.png",n);Image expected,actual;if(FAILED(image_open(&expected,(root/original).c_str()))||FAILED(image_open(&actual,(root/lossless).c_str()))||expected.width!=actual.width||expected.height!=actual.height||expected.bytes!=actual.bytes||memcmp(expected.pixels.get(),actual.pixels.get(),expected.bytes)!=0) { log_line(L"recording regression failed at line %d",__LINE__);return false; } }
    Timeline one_frame=range;one_frame.first=1;one_frame.last=2;
    for(bool fast:{true,false}) { auto file=root/(fast?L"one-frame-fast.gif":L"one-frame-precise.gif");if(FAILED(encode(source,file,one_frame,1,0,12,1,control,{}, {},fast))) { log_line(L"recording regression failed at line %d",__LINE__);return false; }Image image;if(FAILED(image_open(&image,file.c_str()))||image.width!=800||image.height!=450||image.count!=1) { log_line(L"recording regression failed at line %d",__LINE__);return false; } }
    capture::Annotation mark;mark.tool=capture::Tool::pen;mark.style.width=20;mark.style.color=RGB(255,0,0);mark.points={{20,20},{50,20}};auto bitmap=capture::overlay(320,180,{mark});auto png=root/L"overlay.png";if(!bitmap||!save_png(bitmap.get(),png.c_str(),true)) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    Image alpha;if(FAILED(image_open(&alpha,png.c_str()))||(alpha.pixels[0]>>24)!=0||(alpha.pixels[20*320+20]>>24)!=255) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    Edits edits{{0,0,160,100},png};if(FAILED(encode(source,root/L"marked.mp4",range,0,0,24,1,control,{},edits))||FAILED(encode(source,root/L"marked.gif",range,1,0,12,1,control,{},edits,false))||FAILED(encode(source,root/L"marked.mkv",range,2,0,24,1,control,{},edits))) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    Image marked;if(FAILED(image_open(&marked,(root/L"marked.gif").c_str()))||marked.width!=160||marked.height!=100) { log_line(L"recording regression failed at line %d",__LINE__);return false; }auto pixel=marked.pixels[20*160+20];if(((pixel>>16)&255)<=220||((pixel>>8)&255)>=30||(pixel&255)>=30) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    // A tiny local PCM fixture avoids depending on an optional FFmpeg sine filter.
    std::vector<BYTE> wave(16044);memcpy(wave.data(),"RIFF",4);memcpy(wave.data()+8,"WAVEfmt ",8);memcpy(wave.data()+36,"data",4);auto put=[&](size_t offset,uint32_t value,int bytes){for(int n=0;n<bytes;++n) wave[offset+size_t(n)]=BYTE(value>>(n*8));};put(4,16036,4);put(16,16,4);put(20,1,2);put(22,1,2);put(24,8000,4);put(28,16000,4);put(32,2,2);put(34,16,2);put(40,16000,4);for(int n=0;n<8000;++n) put(44+size_t(n)*2,uint16_t(int16_t(std::sin(n*6.283185307179586*440./8000.)*8000)),2);
    { unique_handle file(CreateFileW((root/L"tone.wav").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));DWORD written=0;if(!file||!WriteFile(file.get(),wave.data(),DWORD(wave.size()),&written,nullptr)||written!=wave.size()) { log_line(L"recording regression failed at line %d",__LINE__);return false; } }
    auto audio_source=root/L"with-audio.mp4";result=media::run({L"-i",source.wstring(),L"-i",(root/L"tone.wav").wstring(),L"-map",L"0:v:0",L"-map",L"1:a:0",L"-c:v",L"copy",L"-c:a",L"aac",L"-shortest",L"-f",L"mp4",audio_source.wstring()},root/L"audio-source.log",control);if(FAILED(result.error)) { log_line(L"recording regression failed at line %d",__LINE__);return false; }
    for(int format:{0,2}) { auto file=root/(format==2?L"audio-export.mkv":L"audio-export.mp4");if(FAILED(encode(audio_source,file,range,format,0,0,1,control,{}, {},true,true))) { log_line(L"recording regression failed at line %d",__LINE__);return false; }MediaInfo audio_info;if(FAILED(probe_media(file,root/(format==2?L"audio-mkv-probe.log":L"audio-mp4-probe.log"),control,audio_info))||!audio_info.audio||audio_info.width!=800||audio_info.height!=450) { log_line(L"recording regression failed at line %d",__LINE__);return false; } }
    auto wait_studio=[&](int timeout){auto began=GetTickCount64();UiStudioState state{};do{studio_state(&state);if(state.phase!=1&&state.phase!=3)return state;Sleep(5);}while(GetTickCount64()-began<ULONGLONG(timeout));return state;};
    scope_exit stop_studio([]{studio_cancel();while(!studio_shutdown())Sleep(5);});
    if(!studio_open(source.c_str())){ log_line(L"recording regression failed at line %d",__LINE__);return false; }auto opened=wait_studio(16000);if(opened.phase!=2||opened.width!=800||opened.height!=450||opened.duration_ms!=1000){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    UiStudioExport request{};request.first_ms=250;request.last_ms=750;request.width=800;request.height=450;request.format=1;request.fps=12;request.gif_width=320;request.gif_fast=1;
    StringCchCopyW(request.destination,4096,source.c_str());if(studio_export(&request)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    auto studio_output=root/L"studio-width.gif";StringCchCopyW(request.destination,4096,studio_output.c_str());request.width=801;if(studio_export(&request)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }request.width=800;
    if(!studio_export(&request)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }auto exported=wait_studio(30000);if(exported.phase!=4||exported.progress!=100){ log_line(L"recording regression failed at line %d",__LINE__);return false; }Image studio_image;if(FAILED(image_open(&studio_image,studio_output.c_str()))||studio_image.width!=320||studio_image.height!=180||studio_image.count!=6){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    if(studio_export(&request)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    auto cancelled=root/L"studio-cancel.gif";StringCchCopyW(request.destination,4096,cancelled.c_str());if(!studio_export(&request)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }studio_cancel();auto stopped=wait_studio(5000);if(stopped.phase!=2||std::filesystem::exists(cancelled)){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    if(!write_utf8_atomic((root/L"studio-checks.json").c_str(),L"{\"async_probe\":true,\"crop_bounds\":true,\"gif_width_and_frames\":true,\"overwrite_refused\":true,\"cancel_without_partial\":true}")){ log_line(L"recording regression failed at line %d",__LINE__);return false; }
    if(encode(source,source,range,2,0,0,1,control)!=E_INVALIDARG) { log_line(L"recording regression failed at line %d",__LINE__);return false; }if(!player_result_runtime_test(source.c_str())){ log_line(L"recording regression failed at line %d",__LINE__);return false; }wchar_t final_hash[65]{};return hash_file(source.c_str(),L"SHA256",final_hash,65)&&!wcscmp(original_hash,final_hash);
}
}
