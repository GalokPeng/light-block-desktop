#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mferror.h>
#include <cstring>
#include <bit>
#include <utility>
#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QVBoxLayout>
#include "../src/media_worker_api.h"

namespace {
constexpr UINT hello_message=WM_APP+10,ready_message=WM_APP+11,error_message=WM_APP+12,control_message=WM_APP+16,position_message=WM_APP+17,duration_message=WM_APP+18,state_message=WM_APP+19;
constexpr GUID display_state={0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
class Surface final : public QWidget,public QAbstractNativeEventFilter {
    UiMediaRequest request_{};
    UiMediaResult* result_{};
    QMediaPlayer player_;
    QVideoWidget* video_{};
    QAudioOutput* audio_{};
    QTimer watch_;
    QElapsedTimer elapsed_;
    HWND handle_{};
    HPOWERNOTIFY display_notify_{};
    HMODULE wts_{};
    bool paused_{},auto_paused_{},sound_{},locked_{},display_off_{},closing_{},ready_queued_{};
    QSize frame_size_;
    qint64 reported_position_{-1},reported_duration_{-1};
    UiMediaRect geometry_{};
    void reply(UINT message,LPARAM value=0) {if(request_.owner) PostMessageW(HWND(request_.owner),message,request_.token,value);}
    void log(QString const& message) {if(request_.log) request_.log(message.toStdWString().c_str());}
    void fail(HRESULT error,QString const& text) {
        if(closing_) return;result_->error=int32_t(error);log(text);reply(error_message,LPARAM(error));close();
    }
    void state() {reply(state_message,(paused_?1:0)|(sound_?0:2));}
    void playback() {
        if(closing_) return;
        if(paused_||auto_paused_) player_.pause();else player_.play();
    }
    void tracks() {
        // Avoid re-entering the backend's native track-discovery signal stack.
        QTimer::singleShot(0,this,[this]{if(closing_) return;int desired=sound_&&!player_.audioTracks().isEmpty()?0:-1;if(player_.activeAudioTrack()!=desired) player_.setActiveAudioTrack(desired);});
    }
    void sound(bool enabled) {
        sound_=enabled;
        if(enabled&&!audio_) {audio_=new QAudioOutput(this);player_.setAudioOutput(audio_);}
        if(!enabled&&audio_) {
            player_.setAudioOutput(nullptr);
            delete std::exchange(audio_,nullptr);
        }
        tracks();state();
    }
    void layout_video() {
        auto mode=request_.desktop?request_.settings.fit:1;
        video_->setAspectRatioMode(mode==0?Qt::KeepAspectRatioByExpanding:mode==2?Qt::IgnoreAspectRatio:Qt::KeepAspectRatio);
        if(mode==3&&frame_size_.isValid()) {
            auto dpr=devicePixelRatioF();QSize size(qRound(frame_size_.width()/dpr),qRound(frame_size_.height()/dpr));
            video_->setGeometry((width()-size.width())/2,(height()-size.height())/2,size.width(),size.height());
        }else video_->setGeometry(rect());
    }
    bool move_surface(bool force=false) {
        if(request_.embed) {
            HWND host=HWND(request_.embed);if(!IsWindow(host)) return false;RECT rect{};GetClientRect(host,&rect);
            UiMediaRect next{0,0,rect.right,rect.bottom};
            if(force||memcmp(&geometry_,&next,sizeof next)) {geometry_=next;SetWindowPos(handle_,nullptr,0,0,next.width,next.height,SWP_NOZORDER|SWP_NOACTIVATE);}
        }else if(request_.desktop) {
            HWND parent=GetParent(handle_);if(!IsWindow(parent)) return false;
            UiMediaRect next{};if(!request_.geometry||!request_.geometry(request_.settings.monitor,&next)) return false;
            if(force||memcmp(&geometry_,&next,sizeof next)) {geometry_=next;POINT point{next.x,next.y};MapWindowPoints(nullptr,parent,&point,1);SetWindowPos(handle_,HWND_BOTTOM,point.x,point.y,next.width,next.height,SWP_NOACTIVATE);}
        }
        return true;
    }
    void first_frame() {
        ready_queued_=false;if(closing_||result_->ready) return;
        if(!video_->videoSink()->videoFrame().isValid()) return;
        if(!move_surface(true)) {fail(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),QStringLiteral("Qt media parent disappeared before first frame"));return;}
        show();ShowWindow(handle_,SW_SHOWNOACTIVATE);move_surface(true);layout_video();
        result_->ready=1;reply(ready_message);state();
    }
    void watch() {
        if(request_.parent&&WaitForSingleObject(HANDLE(request_.parent),0)!=WAIT_TIMEOUT) {close();return;}
        if(!move_surface()) {fail(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),QStringLiteral("Qt media parent disappeared"));return;}
        if(request_.desktop) {
            SYSTEM_POWER_STATUS power{};bool battery=request_.settings.battery_pause&&GetSystemPowerStatus(&power)&&power.ACLineStatus==0;
            bool paused=locked_||display_off_||battery||
                (request_.settings.fullscreen_pause&&request_.fullscreen&&request_.fullscreen(uintptr_t(handle_),request_.settings.monitor));
            if(result_->ready&&paused!=auto_paused_) {auto_paused_=paused;playback();}
        }
        if(request_.embed&&result_->ready) {
            auto position=player_.position(),duration=player_.duration();
            if(position!=reported_position_) {reported_position_=position;reply(position_message,LPARAM(position));}
            if(duration!=reported_duration_) {reported_duration_=duration;reply(duration_message,LPARAM(duration));}
        }
        if(!result_->ready&&elapsed_.elapsed()>14000) {fail(HRESULT_FROM_WIN32(ERROR_TIMEOUT),QStringLiteral("Qt FFmpeg first frame timed out"));return;}
        if(request_.probe_milliseconds>0&&elapsed_.elapsed()>request_.probe_milliseconds) close();
    }
protected:
    bool nativeEventFilter(QByteArray const&,void* raw,qintptr* result) override {
        auto message=static_cast<MSG*>(raw);if(message->message!=WM_LBUTTONDOWN||!request_.embed)return false;
        HWND root=GetAncestor(HWND(request_.embed),GA_ROOT);if(GetAncestor(message->hwnd,GA_ROOT)!=root)return false;wchar_t type[128]{};GetClassNameW(root,type,128);if(wcscmp(type,L"LightBlockDesktopNative.RecordingFrame"))return false;
        POINT point{short(LOWORD(message->lParam)),short(HIWORD(message->lParam))};if(!ClientToScreen(message->hwnd,&point))return false;
        ReleaseCapture();PostMessageW(root,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(point.x,point.y));*result=0;return true;
    }
    bool nativeEvent(QByteArray const& type,void* raw,qintptr* result) override {
        auto message=static_cast<MSG*>(raw);
        if(message->message==control_message) {
            switch(message->wParam) {
            case 1:paused_=!paused_;playback();break;
            case 2:sound(!sound_);break;
            case 3:player_.setPosition(qBound<qint64>(0,qint64(message->lParam),player_.duration()));break;
            case 4:if(request_.settings_changed) request_.settings_changed(&request_.settings);move_surface(true);layout_video();watch();break;
            case 5:paused_=message->lParam!=0;playback();break;
            case 6:move_surface(true);layout_video();break;
            default:break;
            }
            state();*result=0;return true;
        }
        if(message->message==WM_WTSSESSION_CHANGE) {if(message->wParam==7) locked_=true;else if(message->wParam==8) locked_=false;watch();}
        if(message->message==WM_POWERBROADCAST&&message->wParam==PBT_POWERSETTINGCHANGE) {
            auto setting=reinterpret_cast<POWERBROADCAST_SETTING*>(message->lParam);
            if(setting&&IsEqualGUID(setting->PowerSetting,display_state)&&setting->DataLength>=sizeof(DWORD)) {DWORD state{};memcpy(&state,setting->Data,sizeof state);display_off_=state==0;watch();}
        }
        return QWidget::nativeEvent(type,raw,result);
    }
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);layout_video();}
    void keyPressEvent(QKeyEvent* event) override {if(request_.embed){HWND root=GetAncestor(HWND(request_.embed),GA_ROOT);wchar_t type[128]{};GetClassNameW(root,type,128);if(!wcscmp(type,L"LightBlockDesktopNative.RecordingFrame")){bool ctrl=event->modifiers().testFlag(Qt::ControlModifier);int key=event->key(),action=key==Qt::Key_Escape?9:key==Qt::Key_Space?6:ctrl&&key==Qt::Key_S?7:ctrl&&key==Qt::Key_C?8:ctrl&&key==Qt::Key_Z?33:ctrl&&key==Qt::Key_Y?34:0;if(action){PostMessageW(root,WM_APP+84,WPARAM(action),0);event->accept();return;}}}if(event->key()==Qt::Key_Escape) close();else if(event->key()==Qt::Key_Space) {paused_=!paused_;playback();state();}else QWidget::keyPressEvent(event);}
    void closeEvent(QCloseEvent* event) override {
        if(!closing_) {closing_=true;watch_.stop();hide();player_.blockSignals(true);video_->videoSink()->blockSignals(true);player_.stop();player_.setSource(QUrl());player_.setVideoOutput(nullptr);player_.setAudioOutput(nullptr);result_->elapsed_ms=uint64_t(elapsed_.elapsed());QTimer::singleShot(0,qApp,&QCoreApplication::quit);}
        event->accept();
    }
public:
    Surface(UiMediaRequest const& request,UiMediaResult* result):request_(request),result_(result) {
        qApp->installNativeEventFilter(this);
        elapsed_.start();setWindowTitle(QStringLiteral("Light Block Desktop"));setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_NativeWindow);
        if(request_.desktop||request_.embed) {setWindowFlags(Qt::FramelessWindowHint|Qt::Tool|Qt::WindowDoesNotAcceptFocus);setAttribute(Qt::WA_TransparentForMouseEvents);}
        else setWindowFlags(Qt::Window);
        setStyleSheet(QStringLiteral("background: black;"));resize(900,540);
        video_=new QVideoWidget(this);video_->setAttribute(Qt::WA_TransparentForMouseEvents);video_->setGeometry(rect());player_.setVideoOutput(video_);player_.setLoops(QMediaPlayer::Infinite);
        QObject::connect(&player_,&QMediaPlayer::tracksChanged,this,[this]{tracks();});
        QObject::connect(&player_,&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,QString const& text){fail(E_FAIL,QStringLiteral("Qt FFmpeg decode failure: ")+text);});
        QObject::connect(&player_,&QMediaPlayer::mediaStatusChanged,this,[this](QMediaPlayer::MediaStatus status){if(status==QMediaPlayer::LoadedMedia&&!player_.hasVideo()) fail(MF_E_INVALIDMEDIATYPE,QStringLiteral("Media has no video track"));});
        QObject::connect(video_->videoSink(),&QVideoSink::videoFrameChanged,this,[this](QVideoFrame const& frame){if(closing_||!frame.isValid()) return;++result_->frames;if(frame_size_!=frame.size()) {frame_size_=frame.size();layout_video();}if(!result_->ready&&!ready_queued_) {ready_queued_=true;QTimer::singleShot(0,this,[this]{first_frame();});}});
        handle_=HWND(winId());reply(hello_message,LPARAM(handle_));
        if(request_.embed) {
            DWORD owner_pid{},host_pid{};GetWindowThreadProcessId(HWND(request_.owner),&owner_pid);GetWindowThreadProcessId(HWND(request_.embed),&host_pid);
            if(!owner_pid||owner_pid!=host_pid) {QTimer::singleShot(0,this,[this]{fail(E_ACCESSDENIED,QStringLiteral("Invalid preview host"));});return;}
            SetWindowLongPtrW(handle_,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);SetWindowLongPtrW(handle_,GWL_EXSTYLE,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE);SetParent(handle_,HWND(request_.embed));
            if(GetParent(handle_)!=HWND(request_.embed)) {QTimer::singleShot(0,this,[this]{fail(E_FAIL,QStringLiteral("Cannot attach preview"));});return;}
        }else if(request_.desktop) {
            if(!request_.attach||!request_.attach(uintptr_t(handle_),request_.settings.monitor)) {QTimer::singleShot(0,this,[this]{fail(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),QStringLiteral("Cannot attach wallpaper"));});return;}
            ShowWindow(handle_,SW_HIDE);
            display_notify_=RegisterPowerSettingNotification(handle_,&display_state,DEVICE_NOTIFY_WINDOW_HANDLE);
            wts_=LoadLibraryExW(L"wtsapi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(wts_) {using Fn=BOOL(WINAPI*)(HWND,DWORD);auto fn=std::bit_cast<Fn>(GetProcAddress(wts_,"WTSRegisterSessionNotification"));if(fn) fn(handle_,0);}
        }
        move_surface(true);sound(false);watch_.setInterval(250);QObject::connect(&watch_,&QTimer::timeout,this,[this]{watch();});watch_.start();
        player_.setSource(QUrl::fromLocalFile(QString::fromWCharArray(request_.path)));player_.play();
    }
    ~Surface() override {
        qApp->removeNativeEventFilter(this);
        if(display_notify_) UnregisterPowerSettingNotification(display_notify_);
        if(wts_) {using Fn=BOOL(WINAPI*)(HWND);auto fn=std::bit_cast<Fn>(GetProcAddress(wts_,"WTSUnRegisterSessionNotification"));if(fn) fn(handle_);FreeLibrary(wts_);}
    }
};
int run(UiMediaRequest const* request,UiMediaResult* result) {
    if(!request||!result||request->version!=LB_MEDIA_WORKER_ABI||request->size!=sizeof(UiMediaRequest)||!request->path||QCoreApplication::instance()) return int(E_INVALIDARG);
    *result={LB_MEDIA_WORKER_ABI,sizeof(UiMediaResult),0,0,0,0};
    try {
        qputenv("QT_MEDIA_BACKEND","ffmpeg");
        if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","windows:fontengine=gdi");
        int argc=1;char executable[]="LightBlockMedia";char* argv[]={executable,nullptr};QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
        Surface surface(*request,result);app.exec();return result->error?4:0;
    }catch(...) {result->error=int32_t(E_FAIL);return 4;}
}
}
extern "C" __declspec(dllexport) UiMediaWorkerExports const* LightBlockMediaWorkerExports() {static UiMediaWorkerExports exports{LB_MEDIA_WORKER_ABI,sizeof(UiMediaWorkerExports),run};return &exports;}
