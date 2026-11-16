#include "ui.hpp"
#include "capture_style.hpp"
#include "../src/recording_ui_api.h"
#include <cmath>
#include <algorithm>

namespace {
QString text(UiRecordingState const& state, char const* zh, char const* en) { return QString::fromUtf8(state.english?en:zh); }
QString clock_text(int64_t value) { auto seconds=std::max<int64_t>(0,value)/1000;return QString("%1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0')); }

class TrimTimeline final : public QWidget {
public:
    UiRecordingState state{};
    int active=2;
    bool dragging=false;
    std::function<void(UiRecordingCommand const&)> changed;
    explicit TrimTimeline(QWidget* parent=nullptr):QWidget(parent) {
        setFixedHeight(56);setMinimumWidth(180);setFocusPolicy(Qt::StrongFocus);
    }
    int64_t milliseconds(int64_t frame) const { return std::min(state.duration_ms,int64_t(std::llround(frame*1000./std::max(1.,state.source_fps)))); }
    double x(int64_t frame) const { return 12.+(width()-24.)*frame/std::max<int64_t>(1,state.frames); }
    int64_t frame_at(double point) const { return std::clamp<int64_t>(std::llround((point-12.)*state.frames/std::max(1,width()-24)),0,state.frames); }
    void apply(int64_t frame) {
        if(!state.frames) return;
        if(active==0) { state.first=std::clamp<int64_t>(frame,0,state.last-1);state.position=state.first; }
        else if(active==1) { state.last=std::clamp<int64_t>(frame,state.first+1,state.frames);state.position=state.last-1; }
        else state.position=std::clamp<int64_t>(frame,state.first,state.last-1);
        update();if(changed) changed({13,active,int(dragging),state.first,state.last,state.position});
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#30363c"));p.drawRoundedRect(QRectF(12,24,width()-24,18),5,5);
        double left=x(state.first),right=x(state.last);p.setBrush(QColor("#43685f"));p.drawRoundedRect(QRectF(left,24,std::max(1.,right-left),18),4,4);
        p.setFont(QFont("Segoe UI",8));p.setPen(QColor("#c5d8d0"));p.drawText(QRectF(12,0,width()/2-12,20),Qt::AlignLeft|Qt::AlignVCenter,QString::number(milliseconds(state.first)/1000.,'f',3)+" s");p.drawText(QRectF(width()/2,0,width()/2-12,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(milliseconds(state.last)/1000.,'f',3)+" s");
        for(int handle=0;handle<2;++handle) { double at=handle==0?left:right;p.setPen(Qt::NoPen);p.setBrush(QColor(active==handle&&hasFocus()?"#b6f0d7":"#88cdb4"));p.drawRoundedRect(QRectF(at-5,21,10,24),3,3);p.setPen(QPen(QColor("#203b30"),1.5));p.drawLine(QPointF(at,28),QPointF(at,38)); }
        double at=x(state.position);p.setPen(QPen(QColor("#f3f7fa"),1.5));p.drawLine(QPointF(at,24),QPointF(at,48));p.setBrush(QColor("#f3f7fa"));p.setPen(Qt::NoPen);p.drawPolygon(QPolygonF{QPointF(at-4,51),QPointF(at+4,51),QPointF(at,46)});
    }
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()!=Qt::LeftButton||!state.frames) return;double point=e->position().x(),first=std::abs(point-x(state.first)),last=std::abs(point-x(state.last));active=std::min(first,last)<=12?(first<=last?0:1):2;dragging=true;setFocus(Qt::MouseFocusReason);apply(frame_at(point));e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override { if(dragging) apply(frame_at(e->position().x())); }
    void mouseReleaseEvent(QMouseEvent* e) override { if(dragging&&e->button()==Qt::LeftButton) { dragging=false;apply(frame_at(e->position().x()));e->accept(); } }
    void keyPressEvent(QKeyEvent* e) override { if(e->key()==Qt::Key_Left||e->key()==Qt::Key_Right) { int64_t frame=active==0?state.first:active==1?state.last:state.position;apply(frame+(e->key()==Qt::Key_Right?1:-1)*(e->modifiers().testFlag(Qt::ShiftModifier)?10:1));e->accept(); }else QWidget::keyPressEvent(e); }
    bool focusNextPrevChild(bool next) override { if((next&&active<2)||(!next&&active>0)){active+=next?1:-1;update();return true;}return QWidget::focusNextPrevChild(next); }
    void focusInEvent(QFocusEvent* e) override { if(e->reason()==Qt::TabFocusReason)active=0;else if(e->reason()==Qt::BacktabFocusReason)active=2;QWidget::focusInEvent(e);update(); }
    void focusOutEvent(QFocusEvent* e) override { QWidget::focusOutEvent(e);update(); }
};

class RecordingPanel final : public QWidget {
public:
    UiRecordingState state{};
    UiRecordingCallback callback{};
    void* context{};
    QFrame *record{},*actions{},*timeline_bar{},*exports{},*status_bar{},*crop_bar{};
    QLabel *timer{},*clock{},*dimensions{},*status_label{},*indicator{};
    QPushButton *stop{};
    QToolButton *play{},*move{},*draw{},*highlight{},*eraser{},*undo{},*redo{},*crop{},*reset{},*cancel{},*save{},*copy{},*trim_reset{},*crop_reset{},*crop_cancel{},*crop_apply{};
    QComboBox *format{},*resolution{},*fps{},*gif_speed{};
    QProgressBar* progress{};
    QShortcut* play_shortcut{};
    QList<QShortcut*> confirm_shortcuts;
    QFrame* audio_bar{};QCheckBox *system_audio{},*microphone{};QToolButton *prepare_cancel{},*preview_sound{};
    TrimTimeline* timeline{};
    RECT anchor{};
    uintptr_t native_owner{};
    bool positioned=false,allowed_visible=true;
    explicit RecordingPanel(UiRecordingState const& initial,UiRecordingCallback listener,void* ctx):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),state(initial),callback(listener),context(ctx) {
        setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);setWindowTitle(t("轻块桌面 · 录屏工具栏","Light Block · Recording tools"));capture_ui::apply(this);
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(4);
        record=capture_ui::strip(this);auto row=capture_ui::row(record);auto dot=new QLabel;indicator=dot;dot->setFixedWidth(12);dot->setText("●");dot->setStyleSheet("color:#ff6975;font-size:13px;");row->addWidget(dot);timer=new QLabel;timer->setMinimumWidth(108);row->addWidget(timer,1);stop=new QPushButton(t("结束录屏","Stop"));stop->setIcon(capture_ui::icon("stop"));stop->setIconSize(QSize(16,16));stop->setFixedHeight(30);stop->setStyleSheet("QPushButton{background:#c75360;color:#fff;border:1px solid #e16a77;border-radius:7px;padding:0 10px;}QPushButton:hover{background:#dd6470;}QPushButton:disabled{color:#b89b9e;background:#51383c;border-color:#665056;}");QObject::connect(stop,&QPushButton::clicked,this,[this]{send(state.phase==0?27:state.phase==1?1:9);});row->addWidget(stop);prepare_cancel=add(row,"cancel",t("取消录屏","Cancel recording"),9);outer->addWidget(record);
        audio_bar=capture_ui::strip(this);row=capture_ui::row(audio_bar);system_audio=new QCheckBox(t("系统声音","System audio"));microphone=new QCheckBox(t("麦克风","Microphone"));for(auto control:{system_audio,microphone}){control->setCursor(Qt::PointingHandCursor);control->setMinimumHeight(30);row->addWidget(control,1);}system_audio->setToolTip(t("录制默认播放设备的系统声音；开始前可开关。","Record the default playback device. Choose before starting."));microphone->setToolTip(t("仅开始录屏后访问默认麦克风，结束时立即释放。","The default microphone opens only on Start and is released on Stop."));QObject::connect(system_audio,&QCheckBox::toggled,this,[this](bool checked){notify({25,checked,0,0,0,0});});QObject::connect(microphone,&QCheckBox::toggled,this,[this](bool checked){notify({26,checked,0,0,0,0});});outer->addWidget(audio_bar);
        actions=capture_ui::strip(this);row=capture_ui::row(actions);play=add(row,"pause",t("暂停 / 播放 · 空格","Pause / play · Space"),6);move=add(row,"move",t("拖动画面移动预览","Drag the picture to move preview"),35);row->addWidget(capture_ui::divider());draw=add(row,"pen",t("画笔标注","Pen"),30);highlight=add(row,"highlight",t("高亮笔","Highlighter"),31);eraser=add(row,"erase",t("局部橡皮擦","Eraser"),32);crop=add(row,"crop",t("裁剪画面","Crop frame"),11);row->addWidget(capture_ui::divider());undo=add(row,"undo",t("撤销 · Ctrl+Z","Undo · Ctrl+Z"),33);redo=add(row,"redo",t("重做 · Ctrl+Y","Redo · Ctrl+Y"),34);reset=add(row,"reset",t("清除标注与画面裁剪","Clear annotations and frame crop"),12);row->addStretch();dimensions=new QLabel;dimensions->setStyleSheet("color:#929da9;padding:0 8px;");row->addWidget(dimensions);row->addWidget(capture_ui::divider());cancel=add(row,"cancel",t("关闭预览 · Esc","Close preview · Esc"),9);outer->addWidget(actions);
        crop_bar=capture_ui::strip(this);row=capture_ui::row(crop_bar);auto hint=new QLabel(t("拖动框选裁剪区域","Drag to select the crop area"));row->addWidget(hint,1);crop_reset=add(row,"reset",t("重新框选 · 右键","Reselect · Right click"),15);crop_cancel=add(row,"cancel",t("取消裁剪 · Esc","Cancel crop · Esc"),16);crop_apply=add(row,"done",t("应用裁剪 · Enter","Apply crop · Enter"),17);outer->addWidget(crop_bar);
        preview_sound=add(capture_ui::row(actions),"",t("试听声音","Listen to audio"),28);preview_sound->setToolButtonStyle(Qt::ToolButtonTextOnly);preview_sound->setFixedWidth(64);
        timeline_bar=capture_ui::strip(this);row=capture_ui::row(timeline_bar);timeline=new TrimTimeline;timeline->setAccessibleName(t("录屏时间裁剪","Recording time trim"));timeline->setToolTip(t("拖动两端裁剪；Tab 切换左端、右端与播放指针。← / → 移动一帧，Shift 移动十帧。","Drag ends to trim; Tab selects the left end, right end and playhead. Arrows move one frame; Shift moves ten."));timeline->changed=[this](UiRecordingCommand const& command){state.first=command.first;state.last=command.last;state.position=command.position;notify(command);};row->addWidget(timeline,1);clock=new QLabel;clock->setMinimumWidth(93);clock->setAlignment(Qt::AlignCenter);clock->setStyleSheet("color:#cbd5df;font-family:'Segoe UI';font-size:11px;");row->addWidget(clock);trim_reset=add(row,"reset",t("取消时间裁剪","Reset time trim"),14);outer->addWidget(timeline_bar);
        exports=capture_ui::strip(this);row=capture_ui::row(exports);format=choice(row,t("导出格式","Export format"),{t("MP4 视频","MP4 video"),t("GIF 动图","GIF animation"),t("无损 MKV","Lossless MKV")},20,112);resolution=choice(row,t("分辨率","Resolution"),{t("原始分辨率","Original size"),"1080p","720p","480p"},21,128);fps=choice(row,t("导出帧率","Export FPS"),{t("原始帧率","Original FPS"),"12 fps","15 fps","24 fps","30 fps","60 fps"},22,104);gif_speed=choice(row,t("GIF 调色","GIF palette"),{t("快速调色","Fast palette"),t("精细调色","Precise palette")},24,118);gif_speed->setToolTip(t("快速调色更省时，精细调色适合丰富渐变；两者均保留所选尺寸和帧率。","Fast palette saves time; precise palette suits rich gradients. Both keep your selected size and frame rate."));row->addWidget(capture_ui::divider());copy=add(row,"copy",t("复制文件 · Ctrl+C","Copy file · Ctrl+C"),8);save=add(row,"save",t("保存录屏 · Ctrl+S","Save recording · Ctrl+S"),7);outer->addWidget(exports);
        status_bar=capture_ui::strip(this);row=capture_ui::row(status_bar);status_label=new QLabel;status_label->setMinimumWidth(0);status_label->setWordWrap(true);status_label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);status_label->setStyleSheet("color:#aab9c8;padding:2px 6px;");row->addWidget(status_label,1);progress=new QProgressBar;progress->setFixedSize(90,6);progress->setTextVisible(false);progress->setStyleSheet("QProgressBar{border:0;border-radius:3px;background:#343b47;}QProgressBar::chunk{border-radius:3px;background:#8ccdb4;}");row->addWidget(progress);outer->addWidget(status_bar);
        for(auto [sequence,action]:{std::pair{QKeySequence(Qt::Key_Space),6},std::pair{QKeySequence(Qt::Key_Escape),9},std::pair{QKeySequence(QKeySequence::Copy),8},std::pair{QKeySequence(QKeySequence::Save),7},std::pair{QKeySequence(QKeySequence::Undo),33},std::pair{QKeySequence(QKeySequence::Redo),34}}) { auto shortcut=new QShortcut(sequence,this);if(action==6)play_shortcut=shortcut;QObject::connect(shortcut,&QShortcut::activated,this,[this,action]{if(action==6){auto focused=QApplication::focusWidget();if(auto button=qobject_cast<QAbstractButton*>(focused);button&&isAncestorOf(button)){button->click();return;}if(auto box=qobject_cast<QComboBox*>(focused);box&&isAncestorOf(box)){box->showPopup();return;}}send(action);}); }
        for(auto key:{Qt::Key_Return,Qt::Key_Enter}){auto shortcut=new QShortcut(QKeySequence(key),this);confirm_shortcuts.append(shortcut);QObject::connect(shortcut,&QShortcut::activated,this,[this]{if(state.phase==0)send(27);else if(state.editing_crop)send(17);});}
        set_state(initial);
    }
    QString t(char const* zh,char const* en) const { return text(state,zh,en); }
    void notify(UiRecordingCommand const& command) { if(callback) callback(context,&command); }
    void send(int action) { if(state.editing_crop&&action==9)action=16;notify({action,0,0,0,0,0}); }
    QToolButton* add(QHBoxLayout* row,QString key,QString title,int action) { auto control=capture_ui::button(key,title,this);row->addWidget(control);QObject::connect(control,&QToolButton::clicked,this,[this,action]{send(action);});return control; }
    QComboBox* choice(QHBoxLayout* row,QString title,QStringList items,int action,int width) { auto field=new capture_ui::ComboBox;field->setEditable(false);field->setAccessibleName(title);field->setToolTip(title);field->addItems(items);field->setMinimumWidth(width);field->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);field->setMinimumHeight(30);row->addWidget(field,1);QObject::connect(field,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,action](int index){notify({action,index,0,0,0,0});});return field; }
    void set_state(UiRecordingState const& next) {
        auto compact=[](int phase){return phase<=2||phase==8||phase==9||phase==10;};bool resizing=compact(state.phase)!=compact(next.phase);state=next;bool recording=compact(state.phase),busy=state.phase!=3;
        bool cropping=state.phase==6&&state.editing_crop;crop_bar->setVisible(cropping);record->setVisible(recording);actions->setVisible(!recording&&!cropping);timeline_bar->setVisible(!recording&&!cropping);exports->setVisible(!recording&&!cropping);
        if(play_shortcut)play_shortcut->setEnabled(state.phase==3);
        for(auto shortcut:confirm_shortcuts)shortcut->setEnabled(state.phase==0||cropping);
        timer->setText(state.phase==0?t("准备录屏","Ready to record"):state.phase==10?t("正在合并音轨…","Combining audio…"):state.phase==8?t("正在读取素材…","Reading media…"):state.phase==2||state.phase==9?t("正在准备预览…","Preparing preview…"):state.stopping?t("正在完成录制…","Finishing…"):clock_text(state.elapsed_ms));stop->setText(state.phase==0?t("开始","Start"):state.phase==1?t("结束录屏","Stop"):t("取消","Cancel"));stop->setIcon(capture_ui::icon(state.phase==0?"record":"stop"));stop->setToolTip(state.stop_hotkey?t("结束录屏 · Ctrl+Alt+R","Stop recording · Ctrl+Alt+R"):QString());stop->setEnabled(state.phase!=1||!state.stopping);indicator->setStyleSheet(state.phase==1?"color:#ff6975;font-size:13px;":"color:#8ccdb4;font-size:13px;");
        prepare_cancel->setVisible(state.phase==0);audio_bar->setVisible(state.phase==0||state.phase==1||state.phase==10);{QSignalBlocker a(system_audio),b(microphone);system_audio->setChecked(state.system_audio!=0);microphone->setChecked(state.microphone!=0);}system_audio->setEnabled(state.phase==0);microphone->setEnabled(state.phase==0);
        preview_sound->setVisible(state.has_audio!=0);preview_sound->setEnabled(!busy);preview_sound->setText(state.preview_muted?t("试听","Listen"):t("静音","Mute"));preview_sound->setToolTip(state.preview_muted?t("播放预览声音，导出保留音轨","Play preview audio. Exports keep the audio track."):t("静音预览，导出仍保留音轨","Mute preview. Exports still keep the audio track."));
        if(!timeline->dragging) { timeline->state=state;timeline->update(); }timeline->setEnabled(!busy);trim_reset->setEnabled(!busy);
        clock->setText(clock_text(std::llround(state.position*1000./std::max(1.,state.source_fps)))+" / "+clock_text(state.duration_ms));dimensions->setText(QString("%1 × %2").arg(state.width).arg(state.height));
        QString play_key=state.paused?"play":"pause";if(play->property("recordingIcon").toString()!=play_key) { play->setIcon(capture_ui::icon(play_key));play->setProperty("recordingIcon",play_key); }play->setEnabled(!busy);move->setEnabled(!busy);draw->setEnabled(!busy);highlight->setEnabled(!busy);eraser->setEnabled(!busy);crop->setEnabled(!busy);undo->setEnabled(!busy&&state.can_undo);redo->setEnabled(!busy&&state.can_redo);reset->setEnabled(!busy);save->setEnabled(!busy);copy->setEnabled(!busy);
        for(auto item:{std::pair{format,state.format},std::pair{resolution,state.resolution},std::pair{fps,state.fps},std::pair{gif_speed,state.gif_fast?0:1}}) { QSignalBlocker block(item.first);item.first->setCurrentIndex(std::clamp(item.second,0,item.first->count()-1));item.first->setEnabled(!busy); }
        gif_speed->setVisible(state.format==1);cancel->setToolTip(state.phase==4?t("取消导出","Cancel export"):t("关闭预览 · Esc","Close preview · Esc"));
        QString message=QString::fromWCharArray(state.status);status_bar->setVisible((!recording&&!cropping&&(busy||!message.isEmpty()))||(state.phase==0&&!message.isEmpty()));status_label->setText(message);status_label->setToolTip(message);progress->setVisible(state.phase==4);progress->setRange(0,state.progress_percent<0?0:100);progress->setValue(std::clamp(state.progress_percent,0,100));
        setFixedWidth(recording?(state.english?330:300):(state.english?640:602));adjustSize();if(positioned&&(resizing||isVisible())) place(anchor,allowed_visible);
    }
    void place(RECT rect,bool visible) {
        anchor=rect;positioned=true;allowed_visible=visible;
        if(!visible) { if(isVisible()) hide();return; }bool revealed=!isVisible();if(revealed) show();adjustSize();HWND native=reinterpret_cast<HWND>(winId());MONITORINFO info{};info.cbSize=sizeof info;GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&info);double ratio=GetDpiForWindow(native)/96.;int w=int(std::ceil(width()*ratio)),h=int(std::ceil(height()*ratio));int x=int((rect.left+rect.right-w)/2),y=rect.bottom+8;x=std::max(int(info.rcWork.left)+6,std::min(x,int(info.rcWork.right)-w-6));if(y+h>info.rcWork.bottom) y=rect.top-h-8;if(y<info.rcWork.top) y=std::max(int(info.rcWork.top)+6,int(info.rcWork.bottom)-h-6);
        if(native_owner&&IsWindow(HWND(native_owner))&&GetWindow(native,GW_OWNER)!=HWND(native_owner))SetWindowLongPtrW(native,GWLP_HWNDPARENT,LONG_PTR(native_owner));bool native_hidden=!testAttribute(Qt::WA_DontShowOnScreen)&&!IsWindowVisible(native);RECT actual{};GetWindowRect(native,&actual);if(revealed||native_hidden||actual.left!=x||actual.top!=y||actual.right-actual.left!=w||actual.bottom-actual.top!=h) SetWindowPos(native,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|(testAttribute(Qt::WA_DontShowOnScreen)?0:SWP_SHOWWINDOW));
    }
protected:
    void closeEvent(QCloseEvent* e) override { e->ignore();send(9); }
};

QHash<quintptr,RecordingPanel*> panels;
uintptr_t create(uintptr_t owner,UiRecordingState const* state,UiRecordingCallback callback,void* context) {
    if(!qApp||!state) return 0;auto panel=new RecordingPanel(*state,callback,context);panel->native_owner=owner;auto handle=uintptr_t(panel->winId());panels.insert(handle,panel);if(owner) SetWindowLongPtrW(reinterpret_cast<HWND>(handle),GWLP_HWNDPARENT,LONG_PTR(owner));SetWindowDisplayAffinity(reinterpret_cast<HWND>(handle),0x11);return handle;
}
void update(uintptr_t handle,UiRecordingState const* state) { if(auto panel=panels.value(handle);panel&&state) panel->set_state(*state); }
void position(uintptr_t handle,int left,int top,int right,int bottom,int visible) { if(auto panel=panels.value(handle)) panel->place(RECT{left,top,right,bottom},visible!=0); }
void destroy(uintptr_t handle) { if(auto panel=panels.take(handle)) { panel->callback=nullptr;delete panel; } }
bool crop_ui_test(QString const& folder){
    UiRecordingState state{};state.phase=3;state.width=800;state.height=450;state.frames=120;state.last=120;state.source_fps=24;QVector<UiRecordingCommand> commands;RecordingPanel panel(state,[](void* ctx,UiRecordingCommand const* command){static_cast<QVector<UiRecordingCommand>*>(ctx)->push_back(*command);},&commands);panel.ensurePolished();auto owner=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"cloaked recording owner",WS_POPUP,0,0,804,454,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);auto handle=HWND(panel.winId());BOOL cloak=TRUE;DWORD hidden=0;bool safe=owner&&SUCCEEDED(DwmSetWindowAttribute(handle,DWMWA_CLOAK,&cloak,sizeof cloak))&&SUCCEEDED(DwmGetWindowAttribute(handle,DWMWA_CLOAKED,&hidden,sizeof hidden))&&hidden;if(!safe){panel.setAttribute(Qt::WA_DontShowOnScreen);if(owner)DestroyWindow(owner);return false;}panel.native_owner=uintptr_t(owner);panel.place(RECT{100,100,904,554},true);bool ok=GetWindow(handle,GW_OWNER)==owner;
    for(int n=0;n<12;++n){state.phase=5;state.editing_crop=0;panel.set_state(state);state.phase=6;state.editing_crop=1;panel.set_state(state);qApp->processEvents();ok=ok&&panel.isVisible()&&IsWindowVisible(handle)&&GetWindow(handle,GW_OWNER)==owner&&panel.crop_bar->isVisible()&&panel.rect().contains(panel.crop_bar->geometry())&&!panel.actions->isVisible()&&!panel.timeline_bar->isVisible()&&!panel.exports->isVisible()&&panel.crop_reset->isEnabled()&&panel.crop_apply->isEnabled();ShowWindow(handle,SW_HIDE);panel.set_state(state);qApp->processEvents();ok=ok&&IsWindowVisible(handle)&&GetWindow(handle,GW_OWNER)==owner;panel.crop_reset->click();ok=ok&&!commands.empty()&&commands.last().action==15;panel.crop_apply->click();ok=ok&&commands.last().action==17;panel.crop_cancel->click();ok=ok&&commands.last().action==16;state.phase=3;state.editing_crop=0;panel.set_state(state);qApp->processEvents();ok=ok&&!panel.crop_bar->isVisible()&&panel.actions->isVisible()&&panel.timeline_bar->isVisible();}
    state.phase=6;state.editing_crop=1;panel.set_state(state);qApp->processEvents();QImage image(panel.size()*panel.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(panel.devicePixelRatioF());image.fill(Qt::transparent);panel.render(&image);bool saved=image.save(folder+"/recording-crop.png");panel.hide();SetWindowLongPtrW(handle,GWLP_HWNDPARENT,0);panel.native_owner=0;DestroyWindow(owner);return saved&&ok;
}
bool audio_ui_test(QString const& folder){
    UiRecordingState state{};state.phase=0;state.width=1280;state.height=720;QList<UiRecordingCommand> commands;
    auto receive=[](void* context,UiRecordingCommand const* command){static_cast<QList<UiRecordingCommand>*>(context)->append(*command);};
    RecordingPanel panel(state,receive,&commands);panel.setAttribute(Qt::WA_DontShowOnScreen);panel.show();QApplication::processEvents();
    bool defaults=!panel.system_audio->isChecked()&&!panel.microphone->isChecked()&&panel.system_audio->isEnabled()&&panel.microphone->isEnabled();
    panel.system_audio->click();bool system=!commands.empty()&&commands.last().action==25&&commands.last().value==1;
    panel.microphone->click();bool microphone=commands.last().action==26&&commands.last().value==1;
    panel.stop->click();bool start=commands.last().action==27;panel.prepare_cancel->click();bool cancel=commands.last().action==9;
    state.system_audio=state.microphone=1;panel.set_state(state);QImage image(panel.size()*panel.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(panel.devicePixelRatioF());image.fill(Qt::transparent);panel.render(&image);bool rendered=image.save(folder+"/recording-ready.png");
    state.phase=1;panel.set_state(state);bool locked=!panel.system_audio->isEnabled()&&!panel.microphone->isEnabled()&&panel.system_audio->isChecked()&&panel.microphone->isChecked();
    state.phase=10;state.audio_merging=1;panel.set_state(state);panel.stop->click();bool merge=commands.last().action==9&&!panel.timeline_bar->isVisible()&&!panel.microphone->isEnabled();
    state.phase=0;wcscpy_s(state.status,L"音频选项未能保存");panel.set_state(state);bool visible_error=panel.status_bar->isVisible()&&!panel.play_shortcut->isEnabled();
    state.phase=3;state.frames=300;state.last=300;state.duration_ms=10000;state.source_fps=30;panel.set_state(state);panel.timeline->active=0;QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(panel.timeline,&tab);bool right_handle=panel.timeline->active==1;QKeyEvent back(QEvent::KeyPress,Qt::Key_Backtab,Qt::ShiftModifier);QApplication::sendEvent(panel.timeline,&back);bool left_handle=panel.timeline->active==0;
    return defaults&&system&&microphone&&start&&cancel&&rendered&&locked&&merge&&visible_error&&right_handle&&left_handle;
}
int tests(wchar_t const* directory) {
    if(!qApp||!directory) return 0;QString folder=QString::fromWCharArray(directory);if(!QDir().mkpath(folder)) return 0;UiRecordingState state{};state.phase=3;state.width=1280;state.height=720;state.source_fps=29.97;state.frames=300;state.last=300;state.duration_ms=10000;state.position=72;state.progress_percent=0;state.gif_fast=1;
    QList<UiRecordingCommand> commands;auto receive=[](void* context,UiRecordingCommand const* command){static_cast<QList<UiRecordingCommand>*>(context)->append(*command);};RecordingPanel panel(state,receive,&commands);panel.ensurePolished();panel.show();QApplication::processEvents();auto render=[&](QString file){panel.adjustSize();QImage image(panel.size()*panel.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(panel.devicePixelRatioF());image.fill(Qt::transparent);panel.render(&image);return image.save(folder+"/"+file);};
    state.can_undo=state.can_redo=1;panel.set_state(state);bool direct_actions=true;for(auto item:{std::pair{panel.draw,30},std::pair{panel.highlight,31},std::pair{panel.eraser,32},std::pair{panel.move,35},std::pair{panel.undo,33},std::pair{panel.redo,34}}){item.first->click();direct_actions=direct_actions&&!commands.empty()&&commands.last().action==item.second;}bool preview=render("recording-preview.png"),select_only=!panel.format->isEditable()&&!panel.resolution->isEditable()&&!panel.fps->isEditable()&&!panel.gif_speed->isEditable();panel.format->setCurrentIndex(1);bool format_signal=!commands.empty()&&commands.last().action==20&&commands.last().value==1;
    state.format=1;state.resolution=2;state.fps=3;panel.set_state(state);bool gif_options=panel.gif_speed->isVisible()&&panel.gif_speed->currentIndex()==0;bool gif_image=render("recording-gif.png");panel.gif_speed->setCurrentIndex(1);bool independent_gif=!commands.empty()&&commands.last().action==24&&commands.last().value==1&&panel.resolution->currentIndex()==2&&panel.fps->currentIndex()==3;
    state.format=2;panel.set_state(state);bool mkv_options=panel.format->count()==3&&panel.format->currentIndex()==2&&!panel.gif_speed->isVisible();bool mkv_image=render("recording-lossless.png");state.format=0;state.resolution=0;state.fps=0;panel.set_state(state);
    panel.timeline->active=0;panel.timeline->apply(40);QKeyEvent arrow(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(panel.timeline,&arrow);bool one_frame=panel.timeline->state.first==41;QKeyEvent shifted(QEvent::KeyPress,Qt::Key_Left,Qt::ShiftModifier);QApplication::sendEvent(panel.timeline,&shifted);bool ten_frames=panel.timeline->state.first==31;panel.timeline->active=1;panel.timeline->apply(0);bool no_cross=panel.timeline->state.last==32;
    panel.save->click();bool save_signal=commands.last().action==7;state.phase=4;state.progress_percent=42;wcscpy_s(state.status,L"正在导出 · 42%");panel.set_state(state);bool busy=!panel.save->isEnabled()&&!panel.timeline->isEnabled()&&panel.cancel->isEnabled();bool export_image=render("recording-export.png");panel.cancel->click();bool cancel_signal=commands.last().action==9;
    state.phase=1;state.elapsed_ms=12000;state.stopping=0;state.stop_hotkey=1;panel.set_state(state);bool record_image=render("recording-active.png");bool shortcut_hint=panel.stop->toolTip().contains("Ctrl+Alt+R");panel.stop->click();bool stop_signal=commands.last().action==1;state.stopping=1;panel.set_state(state);bool finishing=!panel.stop->isEnabled();state.phase=8;state.stop_hotkey=0;panel.set_state(state);bool preparing=panel.stop->isEnabled()&&!panel.timeline_bar->isVisible();panel.stop->click();bool probe_cancel=commands.last().action==9;state.phase=3;state.english=1;state.status[0]=0;RecordingPanel english(state,nullptr,nullptr);english.ensurePolished();english.show();QApplication::processEvents();QImage image(english.size()*english.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(english.devicePixelRatioF());image.fill(Qt::transparent);english.render(&image);bool en=image.save(folder+"/recording-preview-en.png");
    QJsonObject result{{"direct_pen_highlight_eraser_move_undo_redo",direct_actions},{"crop_toolbar_reselect_apply_cancel_12_cycles",crop_ui_test(folder)},{"audio_prepare_start_lock_merge_cancel",audio_ui_test(folder)},{"preview_render",preview},{"record_render",record_image},{"export_render",export_image},{"english_render",en},{"gif_render",gif_image},{"lossless_render",mkv_image},{"gif_options_only_for_gif",gif_options&&mkv_options},{"gif_palette_independent_of_size_and_fps",independent_gif},{"lossless_mkv_available",mkv_options},{"noneditable_choices",select_only},{"format_command",format_signal},{"one_frame_keyboard",one_frame},{"shift_ten_frames",ten_frames},{"trim_handles_do_not_cross",no_cross},{"save_command",save_signal},{"busy_disables_editing",busy},{"cancel_command",cancel_signal},{"stop_command",stop_signal},{"stop_disabled_when_finalizing",finishing},{"recording_shortcut_hint",shortcut_hint},{"metadata_probe_cancellable",preparing&&probe_cancel}};QSaveFile file(folder+"/recording-ui-checks.json");bool written=file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(result).toJson())>0&&file.commit();for(auto it=result.begin();it!=result.end();++it) if(!it.value().toBool()) return 0;return written?1:0;
}
}
extern "C" __declspec(dllexport) UiRecordingExports const* LightBlockRecordingUiExports() {
    static UiRecordingExports exports{LB_RECORDING_UI_ABI,sizeof(UiRecordingExports),create,update,position,destroy,tests};return &exports;
}
