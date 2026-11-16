#include "lb.h"
#include "online_catalog_ui_api.h"
#include "migration.hpp"
#include "profile.hpp"
#include "thumbnails.hpp"
#include "static_wallpaper.hpp"
#include "json.h"
#include "network.h"
#include "organizer.hpp"
#include "organizer_geometry.hpp"
#include "window_shape.hpp"
#include "desktop_surface.hpp"
#include "ui_motion.hpp"
bool settings_display_tests();
bool settings_runtime_tests();
bool settings_startup_tests();
bool system_appearance_tests();
bool media_embed_layout_tests();
bool capture_pin_tests();
bool player_result_tests();
#include "annotation.hpp"
#include "recording.hpp"
#include "recording_audio.hpp"
#include "timeline.hpp"
#include "scroll_capture.hpp"
#include "pointer.hpp"
#include "taskbar.hpp"
#include "taskbar_recovery.hpp"
#include "sqlite.hpp"
#include "clipboard.hpp"
#include "calculator.hpp"
#include "rename.hpp"
#include "file_tools.hpp"
#include "focus.hpp"
#include "particles.hpp"
#include "frame_timing.hpp"
#include "capture_gate.hpp"
#include <psapi.h>
static UINT passes,failures;
static wchar_t checks[12000];
static void check(BOOL result,const wchar_t *name) {
    if(passes+failures) StringCchCatW(checks,ARRAYSIZE(checks),L",\n");
    wchar_t line[350];StringCchPrintfW(line,350,L"    {\"name\":\"%s\",\"passed\":%s}",name,result?L"true":L"false");StringCchCatW(checks,ARRAYSIZE(checks),line);
    if(result) ++passes;else ++failures;
}
static uint32_t hash_image(const Image *i) { uint32_t h=2166136261u;for(size_t n=0;n<i->bytes/4;n+=17) h=(h^i->pixels[n])*16777619u;return h; }
int run_self_tests(const wchar_t *report) {
    passes=failures=0;checks[0]=0;
    check(player_release_tests((std::filesystem::path(g_app.data)/L"release-fixture").c_str()),L"safe-wallpaper-release-before-delete");
    check(player_result_tests(),L"independent-recording-result-player-tokens-and-decoder-limit");
    check(lb::taskbar::recovery_tests((std::filesystem::path(g_app.data)/L"taskbar-recovery-fixture").wstring()),L"taskbar-persistent-recovery-ownership");
    check(lb::thumbnails::cache_tests(),L"bounded-native-thumbnail-cache");
    check(lb::profile::tests(std::filesystem::path(g_app.data)/L"profile-fixture"),L"compatible-profile-selection-and-desktop-menu");
    check(lb::migration::tests(std::filesystem::path(g_app.data)/L"migration-fixture"),L"python-v1-profile-migration");
    check(lb::static_wallpaper::tests((std::filesystem::path(g_app.data)/L"static-fixture").c_str()),L"static-wallpaper-persist-rollback-cancel");
    wchar_t joined[LB_PATH];check(path_join(joined,LB_PATH,L"C:\\数据 目录",L"图片.jpg") && !wcscmp(joined,L"C:\\数据 目录\\图片.jpg"),L"unicode-path");
    wchar_t small[3];check(!path_join(small,3,L"C:\\long",L"file"),L"path-overflow-rejected");
    const wchar_t *args[]={L"",L"C:\\路径 with spaces\\",L"a\"quoted\\value",L"\\\\server\\share\\",L"simple"};
    for(UINT n=0;n<ARRAYSIZE(args);++n) { wchar_t quoted[1000],command[1100];quote_arg(quoted,1000,args[n]);StringCchPrintfW(command,1100,L"test.exe %s",quoted);int count=0;wchar_t **parsed=CommandLineToArgvW(command,&count);check(parsed && count==2 && !wcscmp(parsed[1],args[n]),L"windows-argument-roundtrip");if(parsed) LocalFree(parsed); }
    check(!quote_arg(small,3,L"too long"),L"argument-overflow-rejected");
    RECT fit=fit_rect(RECT{0,0,400,400},1920,1080,FALSE);check(fit.left==0 && fit.top==87 && fit.right==400 && fit.bottom==312,L"aspect-fit");
    check(valid_capture_hotkey(MOD_CONTROL|MOD_ALT,'A')&&valid_capture_hotkey(MOD_WIN,'A')&&valid_capture_hotkey(0,VK_F10)&&valid_capture_hotkey(0,VK_SNAPSHOT)&&!valid_capture_hotkey(0,'A')&&!valid_capture_hotkey(MOD_SHIFT,'A')&&!valid_capture_hotkey(0,VK_LEFT)&&!valid_capture_hotkey(MOD_CONTROL,VK_F12)&&!valid_capture_hotkey(MOD_CONTROL|MOD_ALT,'R')&&!valid_capture_hotkey(MOD_CONTROL|MOD_ALT,'V')&&!valid_capture_hotkey(MOD_CONTROL|MOD_ALT,VK_SPACE),L"safe-capture-shortcuts-and-reserved-tools");
    check(settings_runtime_tests(),L"hotkey-conflict-rollback-and-persisted-wallpaper-audio-isolation");
    check(lb::frame_wait(100,133,33)==33&&lb::frame_wait(120,133,33)==13&&lb::frame_wait(160,133,33)==1&&lb::next_frame_due(160,33)==193,L"gif-frame-clock-includes-decode-without-catchup-burst");
    check(media_kind(L"X.GIF")==MEDIA_GIF && media_kind(L"x.MP4")==MEDIA_VIDEO && media_kind(L"x.jpeg")==MEDIA_IMAGE && media_kind(L"evil.exe")==MEDIA_UNKNOWN,L"media-filter");
    check(media_embed_layout_tests(),L"embedded-video-centered-wide-square-portrait-host-gif-keeps-canvas");
    const char *valid="{\"title\":\"\\u4e2d\\u6587 \\ud83c\\udf19\",\"list\":[1,-2,true,null],\"bytes\":20000000}";Json json;BOOL parsed=json_parse(&json,valid,strlen(valid));wchar_t title[60];check(parsed && json_string(&json,json_property(&json,0,"title"),title,60) && !wcscmp(title,L"中文 🌙"),L"json-unicode-surrogate-pair");check(parsed && json_integer(&json,json_property(&json,0,"bytes"),0)==20000000,L"json-integer");if(parsed) json_free(&json);
    const char *invalid_json[]={"{\"x\":1,}","[1,]","{\"x\":01}","{\"x\":1e}","true false","{\"x\":\"\\q\"}"};for(UINT n=0;n<ARRAYSIZE(invalid_json);++n) { BOOL accepted=json_parse(&json,invalid_json[n],strlen(invalid_json[n]));check(!accepted,L"json-invalid-rejected");if(accepted) json_free(&json); }
    check(safe_filename(L"合法 wallpaper.jpg") && !safe_filename(L"../bad.jpg") && !safe_filename(L"CON.txt") && !safe_filename(L"x.exe:stream") && !safe_filename(L"LPT1.jpg"),L"download-filename-boundary");
    check(safe_https_url(L"https://upload.wikimedia.org/example.jpg") && !safe_https_url(L"http://example.com/x") && !safe_https_url(L"https://user:password@example.com/x") && !safe_https_url(L"https://example.com/\r\n"),L"https-url-boundary");
    check(online_catalog_test()==105,L"licensed-catalog-105-entries");
    check(online_catalog_refresh_tests((std::filesystem::path(g_app.data)/L"catalog-refresh-fixture").c_str()),L"online-catalog-atomic-refresh");
    check(lb::organizer::model_tests((std::filesystem::path(g_app.data)/L"organizer-fixture").wstring()),L"organizer-model-group-sort-auto-collect");
    check(lb::organizer::transition_tests(),L"organizer-retiring-lease-keeps-new-intent");
    check(lb::organizer::settings_group_tests((std::filesystem::path(g_app.data)/L"group-settings-fixture").wstring()),L"group-settings-empty-name-color-roundtrip-remove-preserves-files");
    check(lb::organizer::entry_tests((std::filesystem::path(g_app.data)/L"organizer-entry-fixture").wstring()),L"organizer-entry-alias-move-restore-reference-drop-rollback");
    check(lb::organizer::interaction_tests((std::filesystem::path(g_app.data)/L"organizer-interaction-fixture").wstring()),L"organizer-menu-drag-auto-collect-stale-target-lifetime");
    check(lb::capture::start_gate_tests(),L"capture-start-waits-two-distinct-frames-coalesces-and-cancels");
    check(settings_startup_tests(),L"startup-isolated-registry-command-roundtrip-validation");
    check(lb::taskbar::shutdown_tests(),L"taskbar-exit-poll-reaps-without-starving-timer");
    check(lb::taskbar::transition_tests(),L"taskbar-reenable-waits-for-restoration");
    check(lb::taskbar::visibility_tests(),L"taskbar-shell-region-workarea-drift-and-edge-reveal");
    check(lb::pointer::transition_tests(),L"pointer-retiring-result-keeps-new-intent");
    check(lb::capture::annotation_tests(),L"annotation-render-hit-move-undo");
    check(lb::capture::visual_parity_tests(),L"annotation-python-fill-open-arrow-highlight-number-visuals");
    check(lb::capture::eraser_tests(),L"annotation-local-eraser-mask-history-mosaic-radius-alpha-parity");
    check(lb::capture::coordinate_tests(),L"capture-source-pixel-coordinates-dpi-zoom-negative-origin");
    check(lb::capture::hit_tests(),L"annotation-outline-font-mosaic-hit-and-noop-history");
    check(lb::capture::history_budget_tests(),L"annotation-history-eight-mib-budget-and-move-transfer");
    check(lb::capture::wheel_tests(),L"capture-discrete-wheel-target-accumulation-number-history-text-gesture");
    check(capture_pin_tests(),L"pin-wheel-opacity-glow-content-position-anchor-zoom-context-and-escape");
    check(lb::capture::editor_tests(),L"capture-empty-text-style-keeps-editor-caret-and-number-counter");
    check(lb::capture::selection_tests(),L"capture-reselect-stale-recording-right-click-and-drag");
    check(lb::capture::selection_visual_tests(),L"capture-initial-selection-no-shadow-display-topology-safe-close");
    check(lb::capture::dialog_tests((std::filesystem::path(g_app.data)/L"capture-dialog-fixture").wstring()),L"capture-save-dialog-retired-owner-no-write");
    check(lb::capture::save_tests((std::filesystem::path(g_app.data)/L"capture-png-save-fixture").wstring()),L"capture-png-extension-final-target-overwrite-and-owner-retirement");
    check(lb::recording::dialog_tests(),L"recording-export-dialog-retired-replaced-closing-session");
    check(lb::recording::crop_controls_tests(),L"recording-crop-confirm-cancel-controls-stay-visible");
    check(lb::recording::parity_tests(),L"recording-python-parity-trim-loop-crop-preview-history-directory-and-copy");
    check(lb::recording::multi_result_tests(),L"recording-multiple-results-active-capture-independent-callbacks-and-job-limits");
    check(lb::timeline_tests(),L"timeline-frame-accurate-bounded-trim");
    check(lb::scroll::tests(),L"scroll-match-sticky-header-bidirectional-deduplicate");
    check(lb::scroll::session_tests(),L"scroll-continuous-wheel-samples-before-settle-cancel-and-undo");
    check(lb::scroll::finish_tests(),L"scroll-finish-drains-overlaps-and-captures-terminal-frame");
    check(lb::scroll::controls_tests(),L"scroll-preview-controls-stable-between-frame-updates");
    check(lb::scroll::source_tests(),L"scroll-source-display-removal-stops-sampling-retains-finish-image");
    check(lb::scroll::ui_bridge_tests(),L"scroll-qt-card-next-page-revision-and-lifetime");
    check(lb::pointer::artwork_tests(std::filesystem::path(g_app.data)/L"cursor-fixture"),L"three-cursor-styles-tint-all-roles-animated-load");
    check(lb::sql::tests(std::filesystem::path(g_app.data)/L"sqlite-fixture.db"),L"system-sqlite-unicode-binary-bind-rollback");
    check(lb::clipboard::tests(std::filesystem::path(g_app.data)/L"clipboard-fixture.db"),L"clipboard-store-deduplicate-pin-retention");
    check(lb::calculator_tests(),L"safe-calculator-precedence-floor-power-input-boundary");
    check(lb::files::rename_tests(std::filesystem::path(g_app.data)/L"rename-fixture"),L"file-rename-preview-cycle-undo-crash-recovery-collision");
    check(lb::files::lifecycle_tests(),L"file-view-destroy-reopen-drops-stale-worker-results");
    check(lb::files::ui_service_tests(std::filesystem::path(g_app.data)/L"file-service-fixture"),L"file-service-preview-rename-cancel-stale-results-and-source-preservation");
    check(lb::focus::tests(),L"focus-timer-work-break-deadlines");
    check(lb::focus::service_tests(std::filesystem::path(g_app.data)/L"focus-service-fixture"),L"focus-history-workspace-preview-persistence-without-desktop-activation");
    check(lb::particles::tests(),L"particle-bounds-gravity-decay-idle-expiry");
    check(lb::particles::reference_tests(),L"432-python-1.0.0-particle-reference-frames");
    check(settings_display_tests(),L"friendly-display-labels-primary-dpi-no-device-path");
    check(system_appearance_tests(),L"night-light-unavailable-policy-and-display-reasons");
    check(lb::organizer::shelf_geometry_tests(),L"organizer-icon-size-hover-bounds-96-to-240-dpi");
    check(lb::organizer::shelf_taskbar_tests(),L"organizer-taskbar-reservation-reopen-resize-alignments-multimonitor");
    check(lb::window_shape::tests(),L"rounded-popup-corners-and-resize-regions-96-to-240-dpi");
    check(lb::desktop_surface::tests(),L"organizer-alpha-corners-premultiplication-dpi-buffer-reuse");
    check(lb::organizer::surface_tests((std::filesystem::path(g_app.data)/L"alpha-surface-fixture").wstring()),L"organizer-real-hwnd-alpha-resize-owned-search-unicode-cleanup");
    check(lb::motion::tests(),L"interruptible-motion-retains-velocity-reduced-motion-idle-settle");
    check(settings_patch_test(),L"single-setting-commit-retains-unrelated-state-and-rolls-back-save-failure");
    check(lb::recording::export_test((std::filesystem::path(g_app.data)/L"recording-fixture").wstring()),L"bundled-video-trim-and-two-gif-export-modes");
    check(lb::recording::audio::tests(std::filesystem::path(g_app.data)/L"recording-audio-fixture"),L"wasapi-timestamp-silence-overlap-and-synthetic-single-dual-audio-mux");
    check(lb::recording::audio::lifetime_tests(std::filesystem::path(g_app.data)/L"recording-audio-lifetime"),L"wasapi-blocked-prepare-release-bounded-self-owned-worker");
    const wchar_t *files[]={L"night-drive.jpg",L"milky-way.jpg",L"mountain-bay.jpg",L"rain-coast.jpg",L"startrail.gif"};
    wchar_t assets[LB_PATH],path[LB_PATH];path_join(assets,LB_PATH,g_app.directory,L"assets");
    for(UINT n=0;n<ARRAYSIZE(files);++n) {
        path_join(path,LB_PATH,assets,files[n]);Image image;HRESULT hr=image_open(&image,path);
        check(SUCCEEDED(hr) && image.pixels && image.width && image.height,L"wic-original-decode");
        if(SUCCEEDED(hr)) {
            if(n==4) { uint32_t first=hash_image(&image);BOOL changed=FALSE,valid=TRUE;UINT count=image.count;
                for(UINT step=0;step<LB_MIN(20,count*2);++step) { hr=image_next(&image);if(FAILED(hr)) { valid=FALSE;break; }if(hash_image(&image)!=first) changed=TRUE; }
                check(count>1 && changed && valid,L"gif-frames-advance");check(image.delay>=10 && image.delay<=655350,L"gif-frame-timing");
            } else {check(image.width==1920 && image.height==1080,L"original-resolution-preserved");check(!image.scratch&&image.scratch_capacity==0,L"still-decodes-without-second-full-size-buffer");}
            image_close(&image);
        }
    }
    Image invalid;path_join(path,LB_PATH,g_app.data,L"invalid.gif");write_utf8_atomic(path,L"not an image");check(FAILED(image_open(&invalid,path)),L"corrupt-image-rejected");DeleteFileW(path);
    HRSRC resource=FindResourceW(g_app.instance,MAKEINTRESOURCEW(100),RT_RCDATA);BOOL fixture_ok=FALSE;
    path_join(path,LB_PATH,g_app.data,L"disposal-fixture.gif");
    if(resource) { DWORD bytes=SizeofResource(g_app.instance,resource),written=0;const void *data=LockResource(LoadResource(g_app.instance,resource));HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(file!=INVALID_HANDLE_VALUE) { fixture_ok=data && WriteFile(file,data,bytes,&written,NULL) && written==bytes;CloseHandle(file); } }
    Image fixture;HRESULT fixture_hr=fixture_ok?image_open(&fixture,path):E_FAIL;
    check(SUCCEEDED(fixture_hr) && fixture.width==2 && fixture.height==2 && fixture.count==4,L"gif-fixture-decode");
    if(SUCCEEDED(fixture_hr)) {
        check(fixture.pixels[0]==0xffff0000u && fixture.pixels[3]==0xffff0000u && fixture.delay==80,L"gif-first-frame");
        image_next(&fixture);check(fixture.pixels[0]==0xff00ff00u && fixture.pixels[1]==0xffff0000u && fixture.delay==100,L"gif-transparent-composition");
        image_next(&fixture);check(fixture.pixels[0]==0xffff0000u && fixture.pixels[3]==0xff0000ffu && fixture.delay==120,L"gif-disposal-restore-previous");
        image_next(&fixture);check(fixture.pixels[0]==0 && fixture.pixels[1]==0xff00ff00u && fixture.pixels[3]==0 && fixture.delay==140,L"gif-disposal-restore-background");
        image_next(&fixture);check(fixture.pixels[0]==0xffff0000u && fixture.pixels[3]==0xffff0000u,L"gif-loop-reset");
        auto scratch=fixture.scratch.get();auto capacity=fixture.scratch_capacity;bool reused=scratch&&capacity<=size_t(fixture.width)*fixture.height;
        for(int n=0;n<40;++n)if(FAILED(image_next(&fixture))||scratch!=fixture.scratch.get()||capacity!=fixture.scratch_capacity)reused=false;
        check(reused,L"gif-delta-frame-buffer-reused-across-ten-loops");image_close(&fixture);
    }
    DeleteFileW(path);
    path_join(path,LB_PATH,assets,L"night-drive.jpg");HBITMAP thumb=image_thumbnail(path,400,225);BITMAP info={};if(thumb) GetObjectW(thumb,sizeof info,&info);check(thumb && info.bmWidth==400 && info.bmHeight==225,L"thumbnail-bounded");
    wchar_t png[LB_PATH];path_join(png,LB_PATH,g_app.data,L"roundtrip.png");BOOL saved=thumb && save_png(thumb,png);Image png_image;HRESULT png_hr=saved?image_open(&png_image,png):E_FAIL;
    check(saved && SUCCEEDED(png_hr) && png_image.width==400 && png_image.height==225,L"png-roundtrip");if(SUCCEEDED(png_hr)) image_close(&png_image);if(thumb) DeleteObject(thumb);DeleteFileW(png);
    wchar_t textfile[LB_PATH];path_join(textfile,LB_PATH,g_app.data,L"原子写入.txt");check(write_utf8_atomic(textfile,L"中文测试\nNative C"),L"atomic-utf8-write");DeleteFileW(textfile);
    path_join(textfile,LB_PATH,g_app.data,L"hash-test.txt");write_utf8_atomic(textfile,L"abc");wchar_t digest[65];check(hash_file(textfile,L"SHA256",digest,65) && !wcscmp(digest,L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),L"sha256-known-vector");check(hash_file(textfile,L"SHA1",digest,65) && !wcscmp(digest,L"a9993e364706816aba3e25717850c26c9cd0d89d"),L"catalog-sha1-known-vector");DeleteFileW(textfile);
    Settings previous=g_app.settings;StringCchCopyW(g_app.settings.active_path,LB_PATH,L"D:\\测试壁纸\\原画质.png");g_app.settings.cursor_scale=175;BOOL config_saved=save_config();ZeroMemory(&g_app.settings,sizeof g_app.settings);load_config();check(config_saved && !wcscmp(g_app.settings.active_path,L"D:\\测试壁纸\\原画质.png") && g_app.settings.cursor_scale==175,L"unicode-settings-roundtrip");WritePrivateProfileStringW(L"pointer",L"cursor_scale",L"9999",g_app.config);load_config();check(g_app.settings.cursor_scale==100,L"settings-range-validation");g_app.settings=previous;save_config();
    wchar_t output[15000];StringCchPrintfW(output,ARRAYSIZE(output),L"{\n  \"passed\":%u,\n  \"failed\":%u,\n  \"checks\":[\n%s\n  ]\n}\n",passes,failures,checks);
    if(report) write_utf8_atomic(report,output);log_line(L"self-test passed=%u failed=%u",passes,failures);
    return failures?1:0;
}

static struct {
    const wchar_t *report;
    BOOL crash,finished;
    UINT phase,step,confirmed,failed,max_children;
    DWORD secondary_pid;BOOL isolated;
    ULONGLONG started,requested,hold,max_first_frame;
    wchar_t wallpaper[LB_PATH];
    HANDLE children[48];DWORD pids[48];UINT child_count;
} stress;
static UINT child_count(void) {
    UINT count=0;for(UINT n=0;n<stress.child_count;++n) if(WaitForSingleObject(stress.children[n],0)==WAIT_TIMEOUT) ++count;return count;
}
static void track_child(void) {
    for(auto *state:{&g_app.player,&g_app.wallpaper}) {
        if(!state->process) continue;bool known=false;for(UINT n=0;n<stress.child_count;++n) if(stress.pids[n]==state->pid) known=true;if(known) continue;
        if(stress.child_count>=ARRAYSIZE(stress.children)) { ++stress.failed;return; }
        HANDLE copy=nullptr;if(DuplicateHandle(GetCurrentProcess(),state->process.get(),GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0)) { stress.children[stress.child_count]=copy;stress.pids[stress.child_count++]=state->pid; }else ++stress.failed;
    }
}
void stress_begin(const wchar_t *report,BOOL crash) {
    ZeroMemory(&stress,sizeof stress);stress.report=report;stress.crash=crash;stress.started=GetTickCount64();
    if(!crash&&!settings_navigation_test()) ++stress.failed;
    SystemParametersInfoW(SPI_GETDESKWALLPAPER,LB_PATH,stress.wallpaper,0);SetTimer(g_app.window,13,50,NULL);
}
static void stress_report(void) {
    PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof memory;GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof memory);
    wchar_t current[LB_PATH];SystemParametersInfoW(SPI_GETDESKWALLPAPER,LB_PATH,current,0);
    if(wcscmp(current,stress.wallpaper)) ++stress.failed;
    if(child_count()) ++stress.failed;
    UINT thumbnails=0;for(UINT n=0;n<g_app.count;++n) if(g_app.items[n].thumb) ++thumbnails;
    if(stress.max_children!=2||!stress.isolated||thumbnails<6) ++stress.failed;
    wchar_t text[1800];StringCchPrintfW(text,1800,L"{\n\"confirmed_switches\":%u,\n\"failures\":%u,\n\"max_children\":%u,\n\"remaining_children\":%u,\n\"independent_channels\":%s,\n\"thumbnails_rendered\":%u,\n\"max_heartbeat_gap_ms\":%llu,\n\"max_first_frame_ms\":%llu,\n\"wallpaper_unchanged\":%s,\n\"working_set\":%llu,\n\"private_bytes\":%llu,\n\"elapsed_ms\":%llu\n}\n",stress.confirmed,stress.failed,stress.max_children,child_count(),stress.isolated?L"true":L"false",thumbnails,g_app.max_gap,stress.max_first_frame,!wcscmp(current,stress.wallpaper)?L"true":L"false",(unsigned long long)memory.WorkingSetSize,(unsigned long long)memory.PrivateUsage,GetTickCount64()-stress.started);
    if(stress.report) write_utf8_atomic(stress.report,text);
    for(UINT n=0;n<stress.child_count;++n) CloseHandle(stress.children[n]);
    KillTimer(g_app.window,13);stress.finished=TRUE;PostMessageW(g_app.window,WM_COMMAND,900,0);
}
void stress_tick(void) {
    if(stress.finished) return;
    ULONGLONG now=GetTickCount64();track_child();stress.max_children=LB_MAX(stress.max_children,child_count());
    if(now-stress.started>65000 || g_app.count<6) { ++stress.failed;player_stop(TRUE);if(!g_app.player.process&&!g_app.wallpaper.process) stress_report();return; }
    if(stress.crash) {
        if(!stress.phase) { player_request(g_app.items[4].path,FALSE);stress.phase=1; }
        else if(g_app.player.ready) { wchar_t text[160];StringCchPrintfW(text,160,L"{\"child_pid\":%lu,\"ready\":true}\n",g_app.player.pid);if(stress.report) write_utf8_atomic(stress.report,text);ExitProcess(99); }
        return;
    }
    if(stress.phase==0) {
        if(stress.step>=12) { player_test_secondary(g_app.items[5].path);stress.requested=now;stress.phase=5;return; }
        static const UINT sequence[]={0,4,5,1,4,5,2,4,5,3,4,5};
        if(stress.step==6) { /* Coalesce a burst before waiting for the latest GIF. */
            player_request(g_app.items[0].path,FALSE);player_request(g_app.items[5].path,FALSE);player_request(g_app.items[1].path,FALSE);
        }
        UINT item=sequence[stress.step];player_request(g_app.items[item].path,FALSE);stress.requested=now;stress.phase=1;
    } else if(stress.phase==1) {
        if(g_app.player.ready && !g_app.player.closing && !g_app.player.pending) {
            ++stress.confirmed;stress.max_first_frame=LB_MAX(stress.max_first_frame,now-stress.requested);stress.hold=now;stress.phase=2;
        } else if(now-stress.requested>17000) { ++stress.failed;player_stop(TRUE);stress.phase=3; }
    } else if(stress.phase==2 && now-stress.hold>300) {
        if(stress.step==7 && g_app.player.process) { /* Fault only the child created by this test. */
            TerminateProcess(g_app.player.process.get(),71);stress.phase=4;
        } else { ++stress.step;stress.phase=0; }
    } else if(stress.phase==4 && !g_app.player.process) { ++stress.step;stress.phase=0; }
    else if(stress.phase==5) {
        if(g_app.player.ready&&g_app.wallpaper.ready) {
            stress.secondary_pid=g_app.wallpaper.pid;DWORD before=g_app.player.pid;player_request(g_app.items[5].path,FALSE);if(before!=g_app.player.pid) ++stress.failed;
            PostMessageW(g_app.wallpaper.window,WM_LB_CONTROL,1,0);stress.phase=8;
        }
        else if(now-stress.requested>15000) { ++stress.failed;player_stop(TRUE);stress.phase=3; }
    }else if(stress.phase==6&&!g_app.player.process) { player_request(g_app.items[4].path,FALSE);stress.phase=7; }
    else if(stress.phase==7&&g_app.player.ready) {
        stress.isolated=g_app.wallpaper.ready&&g_app.wallpaper.pid==stress.secondary_pid&&!g_app.wallpaper.pending_desktop&&g_app.wallpaper.paused&&!g_app.player.paused&&g_app.wallpaper.muted&&g_app.player.muted;
        player_stop(TRUE);stress.phase=3;
    }else if(stress.phase==8) {
        if(g_app.wallpaper.paused&&!g_app.player.paused) { player_refresh_settings();player_stop_preview();stress.phase=6; }
        else if(now-stress.requested>15000) { ++stress.failed;player_stop(TRUE);stress.phase=3; }
    }
    else if(stress.phase==3&&!g_app.player.process&&!g_app.wallpaper.process) stress_report();
}
