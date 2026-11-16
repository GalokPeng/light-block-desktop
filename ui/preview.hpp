#pragma once
#include "ui.hpp"
#include <cstring>

namespace ui {
namespace preview_detail {
struct Decoration {
    QSize size;
    qreal ratio{1};
    QString title,type;
    bool dark{},english{},playing{};
    bool operator==(Decoration const&)const=default;
};
inline void paint_decoration(QPainter& p,Decoration const& d){
    QRectF area(QPointF(0,0),QSizeF(d.size));QPainterPath clip;clip.addRoundedRect(area.adjusted(.5,.5,-.5,-.5),14,14);
    p.save();p.setRenderHint(QPainter::Antialiasing);p.setClipPath(clip);
    QLinearGradient shade(0,d.size.height()*.4,0,d.size.height());shade.setColorAt(0,QColor(0,0,0,0));shade.setColorAt(1,QColor(5,12,22,210));p.fillRect(area,shade);
    if(!d.title.isEmpty()){
        p.setPen(Qt::NoPen);p.setBrush(QColor(15,24,33,160));QRectF tag(18,18,d.english?152:116,27);p.drawRoundedRect(tag,13,13);p.setPen(QColor("#f0f7f4"));p.setFont(QFont("Microsoft YaHei UI",9));p.drawText(tag,Qt::AlignCenter,d.type+(d.english?QString("  /  Original"):QString::fromUtf8("  /  原文件")));
        p.setFont(QFont("Microsoft YaHei UI",20,QFont::Medium));p.drawText(QRectF(24,d.size.height()-83,d.size.width()-48,38),Qt::AlignVCenter,p.fontMetrics().elidedText(d.title,Qt::ElideRight,std::max(1,d.size.width()-48)));
        QString subtitle=d.english?QString("A fresh view every time you return."):QString::fromUtf8("让每一次回到桌面，都有一点不同。");
        if(d.type=="GIF")subtitle=d.english?QString("Animated GIF preview · Loops on your desktop"):QString::fromUtf8("GIF 动态预览 · 应用后在桌面循环播放");
        else if(d.type=="Video"||d.type==QString::fromUtf8("视频"))subtitle=d.playing?(d.english?QString("Video preview · Plays on your desktop"):QString::fromUtf8("视频动态预览 · 应用后在桌面播放")):(d.english?QString("Cover preview · Plays on your desktop"):QString::fromUtf8("封面预览 · 应用后在桌面播放"));
        p.setFont(QFont("Microsoft YaHei UI",9));p.setPen(QColor("#c7d4dd"));p.drawText(QRectF(24,d.size.height()-43,d.size.width()-48,23),Qt::AlignVCenter,subtitle);
    }
    p.setClipping(false);p.setBrush(Qt::NoBrush);p.setPen(QColor(d.dark?"#2a3340":"#d8e2dd"));p.drawPath(clip);p.restore();
}

// A native video host occludes its QWidget parent's backing store. This child
// is a sibling of that host, not another decoder or a topmost desktop window.
// Its premultiplied ARGB DIB changes only with the decoration/size/DPI key.
class NativeDecoration final {
    HWND window_{};HDC dc_{};HBITMAP bitmap_{};HGDIOBJ previous_{};void* bits_{};
    QSize pixels_;Decoration key_;bool cached_{},uploaded_{};DWORD error_{};uint64_t uploads_{};
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto self=reinterpret_cast<NativeDecoration*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<NativeDecoration*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window_=window;}
        switch(message){
        case WM_NCHITTEST:return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(window,&ps);EndPaint(window,&ps);return 0;}
        case WM_NCDESTROY:if(self&&self->window_==window)self->window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }return DefWindowProcW(window,message,wp,lp);
    }
    void release_bitmap(){if(dc_&&previous_)SelectObject(dc_,previous_);previous_=nullptr;if(bitmap_)DeleteObject(bitmap_);bitmap_=nullptr;bits_=nullptr;pixels_={};cached_=uploaded_=false;}
    bool create(HWND parent){
        if(window_&&GetParent(window_)==parent)return true;destroy();if(!parent||!IsWindow(parent))return false;
        WNDCLASSEXW cls{};cls.cbSize=sizeof cls;cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LightBlockDesktop.PreviewDecoration";
        if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){error_=GetLastError();return false;}
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_CHILD,0,0,1,1,parent,nullptr,cls.hInstance,this);if(!window_){error_=GetLastError();return false;}return true;
    }
public:
    NativeDecoration()=default;NativeDecoration(NativeDecoration const&)=delete;NativeDecoration& operator=(NativeDecoration const&)=delete;
    ~NativeDecoration(){destroy();}
    void destroy(){if(window_&&IsWindow(window_))DestroyWindow(window_);window_=nullptr;release_bitmap();if(dc_)DeleteDC(dc_);dc_=nullptr;}
    void hide(){if(window_)ShowWindow(window_,SW_HIDE);}
    HWND handle()const{return window_;}
    bool healthy()const{return window_&&uploaded_;}
    DWORD error()const{return error_;}
    uint64_t uploads()const{return uploads_;}
    QSize pixels()const{return pixels_;}
    QImage image()const{return bits_?QImage(static_cast<uchar const*>(bits_),pixels_.width(),pixels_.height(),pixels_.width()*4,QImage::Format_ARGB32_Premultiplied).copy():QImage{};}
    bool update(HWND parent,Decoration const& key,bool visible){
        if(!visible){hide();return true;}if(!create(parent))return false;
        RECT client{};if(!GetClientRect(parent,&client)||client.right<=0||client.bottom<=0)return false;QSize physical(client.right,client.bottom);
        if(physical.width()>16384||physical.height()>16384||qint64(physical.width())*physical.height()>32*1024*1024){hide();error_=ERROR_NOT_ENOUGH_MEMORY;return false;}
        if(!dc_)dc_=CreateCompatibleDC(nullptr);if(!dc_){error_=GetLastError();return false;}
        if(!bitmap_||pixels_!=physical){release_bitmap();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=physical.width();info.bmiHeader.biHeight=-physical.height();info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;bitmap_=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits_,nullptr,0);if(!bitmap_){error_=GetLastError();hide();return false;}pixels_=physical;previous_=SelectObject(dc_,bitmap_);}
        bool changed=!cached_||!(key_==key);
        if(changed){QImage image(static_cast<uchar*>(bits_),pixels_.width(),pixels_.height(),pixels_.width()*4,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);painter.scale(double(physical.width())/std::max(1,key.size.width()),double(physical.height())/std::max(1,key.size.height()));paint_decoration(painter,key);painter.end();key_=key;cached_=true;uploaded_=false;}
        // SetWindowPos uses parent-client coordinates. Keeping pptDst null avoids
        // UpdateLayeredWindow's documented screen-coordinate position argument.
        RECT current{};GetWindowRect(window_,&current);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&current),2);
        if(current.left||current.top||current.right!=physical.width()||current.bottom!=physical.height()||GetTopWindow(parent)!=window_)SetWindowPos(window_,HWND_TOP,0,0,physical.width(),physical.height(),SWP_NOACTIVATE);
        if(!uploaded_){POINT origin{};SIZE size{physical.width(),physical.height()};BLENDFUNCTION alpha{AC_SRC_OVER,0,255,AC_SRC_ALPHA};if(!UpdateLayeredWindow(window_,nullptr,nullptr,&size,dc_,&origin,0,&alpha,ULW_ALPHA)){error_=GetLastError();hide();return false;}uploaded_=true;error_=0;++uploads_;}
        if(!(GetWindowLongPtrW(window_,GWL_STYLE)&WS_VISIBLE))ShowWindow(window_,SW_SHOWNOACTIVATE);return true;
    }
};
}

class Preview:public QWidget {
    preview_detail::NativeDecoration decoration_;bool syncing_{},destroying_{},resize_queued_{};
    void request_media_geometry(){
        if(resize_queued_||destroying_)return;resize_queued_=true;
        QTimer::singleShot(0,this,[this]{resize_queued_=false;if(destroying_||!host||host->isHidden())return;
            // Qt has now committed the host's physical client rectangle. Ask
            // only its foreign media child to refit; the periodic native check
            // remains a fallback for external/native geometry changes.
            auto parent=HWND(host->winId());for(auto child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){DWORD pid{};GetWindowThreadProcessId(child,&pid);if(pid&&pid!=GetCurrentProcessId())PostMessageW(child,WM_APP+16,6,0);}
        });
    }
    preview_detail::Decoration decoration_key()const{return {size(),devicePixelRatioF(),title,type,state.dark!=0,state.english!=0,host&&!host->isHidden()};}
    void sync_decoration(){
        if(syncing_||destroying_||!host||!play)return;QScopedValueRollback<bool> guard(syncing_,true);
        bool visible=isVisible()&&!host->isHidden();if(!visible){decoration_.hide();return;}
        auto parent=HWND(winId());if(GetParent(HWND(host->winId()))!=parent)return;decoration_.update(parent,decoration_key(),true);
    }
protected:
    bool event(QEvent* event)override{
        bool result=QWidget::event(event);switch(event->type()){case QEvent::Show:case QEvent::PaletteChange:case QEvent::ApplicationPaletteChange:case QEvent::DevicePixelRatioChange:case QEvent::WinIdChange:sync_decoration();request_media_geometry();break;case QEvent::Hide:decoration_.hide();break;default:break;}return result;
    }
    bool eventFilter(QObject* object,QEvent* event)override{
        if(object==host){switch(event->type()){case QEvent::Show:case QEvent::ShowToParent:case QEvent::Resize:case QEvent::Move:case QEvent::ZOrderChange:case QEvent::WinIdChange:QTimer::singleShot(0,this,[this]{sync_decoration();});request_media_geometry();break;case QEvent::Hide:case QEvent::HideToParent:decoration_.hide();break;default:break;}}
        return QWidget::eventFilter(object,event);
    }
    void resizeEvent(QResizeEvent*)override{
        if(!host||!play)return;host->setGeometry(rect().adjusted(1,1,-1,-1));QPainterPath mask;mask.addRoundedRect(QRectF(host->rect()),13,13);host->setMask(QRegion(mask.toFillPolygon().toPolygon()));play->adjustSize();play->move((width()-play->width())/2,(height()-play->height())/2);sync_decoration();
    }
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPainterPath clip;clip.addRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),14,14);p.fillPath(clip,QColor("#0e141d"));p.setClipPath(clip);if(!image.isNull()){bool motion=type=="GIF"||type=="Video"||type==QString::fromUtf8("视频");auto scaled=image.size().scaled(size(),motion?Qt::KeepAspectRatio:Qt::KeepAspectRatioByExpanding);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(QRect(QPoint((width()-scaled.width())/2,(height()-scaled.height())/2),scaled),image);}p.setClipping(false);preview_detail::paint_decoration(p,decoration_key());p.end();sync_decoration();
    }
public:
    QImage image;QString title,type;QWidget* host{};QPushButton* play{};std::function<void()> start;
    explicit Preview(QWidget* parent=nullptr):QWidget(parent){setMinimumHeight(258);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setAttribute(Qt::WA_NativeWindow);host=new QWidget(this);host->setAttribute(Qt::WA_NativeWindow);host->setStyleSheet("background:#0c1016;");host->hide();play=button(ui::tr("▶  播放预览","▶  Play preview"),[this]{if(start)start();},this,"primary");play->hide();host->installEventFilter(this);}
    ~Preview()override{destroying_=true;if(host)host->removeEventFilter(this);decoration_.destroy();}
    static bool fixture_tests(QString const& directory);
};
inline bool Preview::fixture_tests(QString const& directory){
    QDir().mkpath(directory);QScopedValueRollback<UiState> preserve(state);state.dark=1;state.english=0;auto fixture=std::make_unique<Preview>();fixture->setAttribute(Qt::WA_DontShowOnScreen);fixture->resize(640,360);fixture->title=QString::fromUtf8("银河 · 动态预览测试");fixture->type="GIF";fixture->show();fixture->host->show();qApp->processEvents();fixture->sync_decoration();
    auto native=HWND(fixture->winId()),host=HWND(fixture->host->winId()),layer=fixture->decoration_.handle();RECT client{};GetClientRect(native,&client);auto image=fixture->decoration_.image();qreal ratio=fixture->devicePixelRatioF();
    bool hidden=!IsWindowVisible(native),healthy=fixture->decoration_.healthy(),siblings=layer&&GetParent(layer)==native&&GetParent(host)==native,top=layer&&GetTopWindow(native)==layer;
    LONG_PTR ex=layer?GetWindowLongPtrW(layer,GWL_EXSTYLE):0,style=layer?GetWindowLongPtrW(layer,GWL_STYLE):0;
    bool styles=(ex&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)&&(style&WS_CHILD)&&!(style&WS_POPUP);
    bool pass_through=layer&&SendMessageW(layer,WM_NCHITTEST,0,0)==HTTRANSPARENT&&SendMessageW(layer,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE;
    bool dimensions=image.size()==QSize(client.right,client.bottom)&&qAbs(image.width()-qRound(640*ratio))<=1&&qAbs(image.height()-qRound(360*ratio))<=1;
    int title_pixels=0;bool alpha=false;
    if(!image.isNull()){auto sx=double(image.width())/640.,sy=double(image.height())/360.;alpha=qAlpha(image.pixel(0,0))==0&&qAlpha(image.pixel(image.width()-int(40*sx),int(10*sy)))==0&&qAlpha(image.pixel(image.width()/2,image.height()-int(6*sy)))>=200;
        for(int y=int(278*sy);y<std::min(image.height(),int(315*sy));++y)for(int x=int(24*sx);x<std::min(image.width(),int(600*sx));++x){auto pixel=image.pixel(x,y);if(qAlpha(pixel)>180&&qRed(pixel)>170&&qGreen(pixel)>170&&qBlue(pixel)>170)++title_pixels;}
    }
    bool screenshots=!image.isNull()&&image.save(directory+"/preview-native-decoration-dib.png");if(!image.isNull()){QImage composite(image.size(),QImage::Format_ARGB32_Premultiplied);composite.fill(QColor("#295f85"));QPainter paint(&composite);paint.drawImage(0,0,image);paint.end();screenshots=composite.save(directory+"/preview-synthetic-frame-decoration.png")&&screenshots;}
    auto uploads=fixture->decoration_.uploads();HWND decoder=CreateWindowExW(0,L"STATIC",L"Hidden decoder child fixture",WS_CHILD|WS_VISIBLE,0,0,client.right,client.bottom,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    for(int n=0;n<40;++n){SetWindowPos(decoder,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);InvalidateRect(decoder,nullptr,FALSE);UpdateWindow(decoder);fixture->sync_decoration();qApp->processEvents();}
    bool cached=decoder&&fixture->decoration_.uploads()==uploads&&GetTopWindow(native)==layer;
    fixture->host->hide();qApp->processEvents();bool host_hidden=layer&&!(GetWindowLongPtrW(layer,GWL_STYLE)&WS_VISIBLE);fixture->host->show();qApp->processEvents();fixture->sync_decoration();bool reshow=fixture->decoration_.healthy()&&(GetWindowLongPtrW(layer,GWL_STYLE)&WS_VISIBLE)&&GetTopWindow(native)==layer&&fixture->decoration_.uploads()==uploads;
    state.dark=0;fixture->sync_decoration();auto light=fixture->decoration_.image();bool theme=fixture->decoration_.uploads()==uploads+1&&light!=image;screenshots=light.save(directory+"/preview-native-decoration-light.png")&&screenshots;
    state.english=1;fixture->title="Night drive · native preview";fixture->type="Video";fixture->sync_decoration();bool text=fixture->decoration_.uploads()==uploads+2&&fixture->decoration_.image()!=light;
    fixture->resize(760,420);qApp->processEvents();fixture->sync_decoration();GetClientRect(native,&client);RECT bounds{};GetWindowRect(layer,&bounds);MapWindowPoints(nullptr,native,reinterpret_cast<POINT*>(&bounds),2);bool resize=fixture->decoration_.pixels()==QSize(client.right,client.bottom)&&bounds.left==0&&bounds.top==0&&bounds.right==client.right&&bounds.bottom==client.bottom&&GetTopWindow(native)==layer;
    fixture->hide();qApp->processEvents();bool parent_hidden=!(GetWindowLongPtrW(layer,GWL_STYLE)&WS_VISIBLE);fixture->show();qApp->processEvents();fixture->sync_decoration();bool parent_shown=fixture->decoration_.healthy()&&(GetWindowLongPtrW(layer,GWL_STYLE)&WS_VISIBLE)&&!IsWindowVisible(native);
    fixture->host->hide();fixture->title.clear();fixture->type="Video";fixture->image=QImage(1600,900,QImage::Format_RGB32);fixture->image.fill(QColor("#ec35d9"));fixture->resize(742,258);qApp->processEvents();QImage wide(fixture->size(),QImage::Format_ARGB32);wide.fill(Qt::transparent);fixture->render(&wide);int first=-1,last=-1;for(int x=1;x<wide.width()-1;++x){auto c=wide.pixelColor(x,80);if(c.red()>160&&c.blue()>120&&c.green()<100){if(first<0)first=x;last=x;}}bool centered_poster=first>100&&last>first&&qAbs(first-(wide.width()-1-last))<=2&&qAbs((last-first+1)-wide.height()*16./9.)<=2;screenshots=wide.save(directory+"/preview-motion-poster-centered.png")&&screenshots;
    fixture->host->show();qApp->processEvents();GetClientRect(HWND(fixture->host->winId()),&client);bool host_full=qAbs(client.right-qRound(fixture->host->width()*fixture->host->devicePixelRatioF()))<=1&&qAbs(client.bottom-qRound(fixture->host->height()*fixture->host->devicePixelRatioF()))<=1&&fixture->host->geometry()==fixture->rect().adjusted(1,1,-1,-1);
    DWORD error=fixture->decoration_.error();fixture.reset();qApp->sendPostedEvents(nullptr,QEvent::DeferredDelete);bool destroyed=!IsWindow(layer)&&!IsWindow(host)&&!IsWindow(decoder)&&!IsWindow(native);
    bool ok=hidden&&healthy&&siblings&&top&&styles&&pass_through&&dimensions&&alpha&&title_pixels>10&&screenshots&&cached&&host_hidden&&reshow&&theme&&text&&resize&&parent_hidden&&parent_shown&&destroyed&&centered_poster&&host_full;
    QJsonObject result{{"passed",ok},{"native_parent_never_visible",hidden&&parent_shown},{"update_layered_window",healthy},{"same_parent_siblings",siblings},{"layer_above_host",top},{"layered_child_styles",styles},{"no_focus_or_hit_capture",pass_through},{"dpi_geometry",dimensions&&resize},{"device_pixel_ratio",ratio},{"transparent_corners_gradient",alpha},{"native_dib_title_pixels",title_pixels},{"forty_native_frames_no_decoration_upload",cached},{"host_hide_reshow",host_hidden&&reshow},{"parent_hide_reshow",parent_hidden&&parent_shown},{"theme_text_invalidate_cache",theme&&text},{"native_children_destroyed",destroyed},{"win32_error",int(error)},{"motion_poster_contain_centered",centered_poster},{"native_host_fills_content_at_dpi",host_full},{"public_desktop_visual_test",false}};
    QSaveFile report(directory+"/preview-native-overlay-checks.json");if(!report.open(QIODevice::WriteOnly))return false;auto bytes=QJsonDocument(result).toJson();return report.write(bytes)==bytes.size()&&report.commit()&&ok;
}
}
