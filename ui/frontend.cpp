#include "ui.hpp"
#include "dialogs.hpp"
#include "updates.hpp"
#include <QtConcurrent>
#include "online.hpp"
#include "pointer_settings.hpp"
#include "group_settings.hpp"
#include "focus.hpp"
#include "file_tools.hpp"
#include "clipboard.hpp"
#include "launcher.hpp"
#include "local_library.hpp"
#include "preview.hpp"
#include "image_loader.hpp"
#include "settings_widgets.hpp"
#include "media_studio.hpp"
#include "../src/system_appearance_ui_api.h"
#include <map>
#include <QAbstractNativeEventFilter>
#include <psapi.h>
#include "../src/capture_ui_api.h"
#include "../src/recording_ui_api.h"
#include "../src/scroll_ui_api.h"
extern "C" UiCaptureExports const* LightBlockCaptureUiExports();
extern "C" UiRecordingExports const* LightBlockRecordingUiExports();
extern "C" UiScrollExports const* LightBlockScrollUiExports();
namespace ui {
UiApi api{};UiState state{};
QPushButton* button(QString text,std::function<void()> action,QWidget* owner,char const* name) { auto b=new QPushButton(text,owner);if(name) b->setObjectName(name);b->setCursor(Qt::PointingHandCursor);QObject::connect(b,&QPushButton::clicked,b,[action]{action();});return b; }
QLabel* label(QString text,char const* name,bool wrap) { auto value=new QLabel(text);if(name) value->setObjectName(name);value->setWordWrap(wrap);return value; }
QIcon icon(int index,bool light) {
    QPixmap image(40,40);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);p.setPen(QPen(QColor(light?"#486e62":"#a2b8b0"),1.35,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(index==0) { p.drawRoundedRect(QRectF(2,3,16,14),2,2);p.drawEllipse(QRectF(11,6,3,3));p.drawPolyline(QPolygonF{{3,14},{7,10},{11,14},{14,11},{17,14}}); }
    else if(index==1) for(int x:{3,11}) for(int y:{3,11}) p.drawRoundedRect(QRectF(x,y,6,6),1.3,1.3);
    else if(index==2) { for(auto pair:{QPoint(3,3),QPoint(17,3),QPoint(3,17),QPoint(17,17)}) { p.drawLine(pair,pair+QPoint(pair.x()==3?5:-5,0));p.drawLine(pair,pair+QPoint(0,pair.y()==3?5:-5)); }p.drawEllipse(QRectF(7,7,6,6)); }
    else if(index==3) for(int x:{4,10,16}) for(int y:{4,10,16}) p.drawEllipse(QRectF(x-1,y-1,2,2));
    else if(index==4) for(auto point:{QPoint(7,5),QPoint(13,10),QPoint(8,15)}) { p.drawLine(3,point.y(),17,point.y());p.setBrush(QColor(light?"#edf3f0":"#151b24"));p.drawEllipse(QRectF(point.x()-2,point.y()-2,4,4)); }
    else if(index==6&&!light) { QPainterPath moon,cut;moon.addEllipse(QRectF(3,3,14,14));cut.addEllipse(QRectF(8.4,1,11,11));p.fillPath(moon.subtracted(cut),QColor("#c5d9e8")); } else if(index==6) { p.drawEllipse(QRectF(6.3,6.3,7.4,7.4));for(auto point:{QPointF(1,0),QPointF(0,1),QPointF(1,1),QPointF(1,-1),QPointF(-1,0),QPointF(0,-1),QPointF(-1,1),QPointF(-1,-1)}) { auto direction=point*(point.x()&&point.y()?.707:1);p.drawLine(QPointF(10,10)+direction*6.5,QPointF(10,10)+direction*8.3); } }
    else { p.drawEllipse(QRectF(2.5,2.5,15,15));p.drawLine(10,9,10,14);p.drawPoint(10,6); }p.end();image.setDevicePixelRatio(2);return QIcon(image);
}
QImage bitmap(uintptr_t handle) { auto source=reinterpret_cast<HBITMAP>(handle);BITMAP metadata{};if(!source||!GetObjectW(source,sizeof metadata,&metadata)||metadata.bmWidth<1||metadata.bmHeight<1) return {};QImage image(metadata.bmWidth,metadata.bmHeight,QImage::Format_RGB32);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),metadata.bmWidth,-metadata.bmHeight,1,32,BI_RGB,0,0,0,0,0};HDC dc=GetDC(nullptr);bool ok=GetDIBits(dc,source,0,metadata.bmHeight,image.bits(),&info,DIB_RGB_COLORS)!=0;ReleaseDC(nullptr,dc);return ok?image:QImage{}; }
class Toggle:public QCheckBox {
public:
    Toggle() { setFixedSize(38,23);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus); }
    bool hitButton(QPoint const& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent*) override { QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(hasFocus()?QPen(QColor("#ceeedd"),1):QPen(Qt::NoPen));p.setBrush(QColor(isChecked()?"#a6e4ca":"#3b4756"));p.drawRoundedRect(QRectF(1,2,36,20),10,10);p.setPen(Qt::NoPen);p.setBrush(QColor(isChecked()?"#18392b":"#b3beca"));p.drawEllipse(QRectF(isChecked()?20:4,5,14,14)); }
};
class NativePane:public QWidget {
    int action_{};uintptr_t child_{};
public:
    explicit NativePane(int action,QWidget* parent=nullptr):QWidget(parent),action_(action) { setAttribute(Qt::WA_NativeWindow); }
    void showEvent(QShowEvent*) override { if(!child_) child_=api.tool(action_,uintptr_t(winId()));if(child_) { ShowWindow(reinterpret_cast<HWND>(child_),SW_SHOWNOACTIVATE);resize_native(); } }
    void resizeEvent(QResizeEvent*) override { resize_native(); }
    void resize_native() { if(child_) { RECT client{};GetClientRect(reinterpret_cast<HWND>(winId()),&client);api.tool_size(action_,client.right,client.bottom); } }
};
class Main; extern std::unique_ptr<Main> main_window;
static int native_modifiers(Qt::KeyboardModifiers modifiers) {
    return (modifiers.testFlag(Qt::ControlModifier)?MOD_CONTROL:0)|(modifiers.testFlag(Qt::AltModifier)?MOD_ALT:0)|(modifiers.testFlag(Qt::ShiftModifier)?MOD_SHIFT:0)|(modifiers.testFlag(Qt::MetaModifier)?MOD_WIN:0);
}
static Qt::KeyboardModifiers qt_modifiers(int modifiers) {
    Qt::KeyboardModifiers result;if(modifiers&MOD_CONTROL)result|=Qt::ControlModifier;if(modifiers&MOD_ALT)result|=Qt::AltModifier;if(modifiers&MOD_SHIFT)result|=Qt::ShiftModifier;if(modifiers&MOD_WIN)result|=Qt::MetaModifier;return result;
}
static int virtual_key(Qt::Key key) {
    if(key>=Qt::Key_F1&&key<=Qt::Key_F24) return VK_F1+int(key)-int(Qt::Key_F1);
    switch(key) { case Qt::Key_Space:return VK_SPACE;case Qt::Key_Tab:return VK_TAB;case Qt::Key_Backspace:return VK_BACK;case Qt::Key_Return:case Qt::Key_Enter:return VK_RETURN;case Qt::Key_Escape:return VK_ESCAPE;case Qt::Key_Insert:return VK_INSERT;case Qt::Key_Delete:return VK_DELETE;case Qt::Key_Home:return VK_HOME;case Qt::Key_End:return VK_END;case Qt::Key_PageUp:return VK_PRIOR;case Qt::Key_PageDown:return VK_NEXT;case Qt::Key_Left:return VK_LEFT;case Qt::Key_Up:return VK_UP;case Qt::Key_Right:return VK_RIGHT;case Qt::Key_Down:return VK_DOWN;case Qt::Key_Print:return VK_SNAPSHOT;default:break; }
    if((key>=Qt::Key_A&&key<=Qt::Key_Z)||(key>=Qt::Key_0&&key<=Qt::Key_9)) return int(key);
    if(int(key)>0&&int(key)<0x10000) { SHORT mapped=VkKeyScanW(wchar_t(key));if(mapped!=-1) return mapped&255; }return 0;
}
static int native_combination(QKeyCombination combination) {
    auto key=combination.key();auto modifiers=combination.keyboardModifiers();int flags=native_modifiers(modifiers),vk{};
    if(modifiers.testFlag(Qt::KeypadModifier)) {
        if(key>=Qt::Key_0&&key<=Qt::Key_9)vk=VK_NUMPAD0+int(key)-int(Qt::Key_0);
        else switch(key){case Qt::Key_Plus:vk=VK_ADD;break;case Qt::Key_Minus:vk=VK_SUBTRACT;break;case Qt::Key_Asterisk:vk=VK_MULTIPLY;break;case Qt::Key_Slash:vk=VK_DIVIDE;break;case Qt::Key_Period:vk=VK_DECIMAL;break;case Qt::Key_Comma:vk=VK_SEPARATOR;break;case Qt::Key_Enter:case Qt::Key_Return:vk=VK_RETURN;break;default:return 0;}
    } else {
        vk=virtual_key(key);
        if(vk&&int(key)>32&&int(key)<0x10000&&!((key>=Qt::Key_A&&key<=Qt::Key_Z)||(key>=Qt::Key_0&&key<=Qt::Key_9))) {
            SHORT translated=VkKeyScanW(wchar_t(key));if(translated!=-1){auto shift=(translated>>8)&255;flags|=(shift&1?MOD_SHIFT:0)|(shift&2?MOD_CONTROL:0)|(shift&4?MOD_ALT:0);}
        }
    }
    return vk?vk|(flags<<16):0;
}
static Qt::Key keypad_key(int vk) {
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return Qt::Key(int(Qt::Key_0)+vk-VK_NUMPAD0);
    switch(vk){case VK_ADD:return Qt::Key_Plus;case VK_SUBTRACT:return Qt::Key_Minus;case VK_MULTIPLY:return Qt::Key_Asterisk;case VK_DIVIDE:return Qt::Key_Slash;case VK_DECIMAL:return Qt::Key_Period;case VK_SEPARATOR:return Qt::Key_Comma;default:return Qt::Key_unknown;}
}
static Qt::Key qt_key(int vk) {
    if(vk>=VK_F1&&vk<=VK_F24) return Qt::Key(int(Qt::Key_F1)+vk-VK_F1);
    switch(vk) { case VK_SPACE:return Qt::Key_Space;case VK_TAB:return Qt::Key_Tab;case VK_BACK:return Qt::Key_Backspace;case VK_RETURN:return Qt::Key_Return;case VK_ESCAPE:return Qt::Key_Escape;case VK_INSERT:return Qt::Key_Insert;case VK_DELETE:return Qt::Key_Delete;case VK_HOME:return Qt::Key_Home;case VK_END:return Qt::Key_End;case VK_PRIOR:return Qt::Key_PageUp;case VK_NEXT:return Qt::Key_PageDown;case VK_LEFT:return Qt::Key_Left;case VK_UP:return Qt::Key_Up;case VK_RIGHT:return Qt::Key_Right;case VK_DOWN:return Qt::Key_Down;case VK_SNAPSHOT:return Qt::Key_Print;default:break; }
    return Qt::Key(MapVirtualKeyW(UINT(vk),MAPVK_VK_TO_CHAR)&0xffff);
}
class Main:public QMainWindow {
    QStackedWidget *pages_{},*tools_{},*library_stack_{};QWidget *root_{};QFrame* sidebar_{};LocalLibraryView* local_grid_{};Preview* preview_{};QLineEdit *search_{};QLabel *meta_{},*monitor_details_{},*status_{},*heading_{},*eyebrow_{};QPushButton* nav_[6]{};QPushButton* library_tabs_[2]{};QPushButton *theme_{},*language_{},*import_{},*github_{},*apply_wallpaper_{};QComboBox *fit_{},*monitor_{};QTimer refresh_;std::map<int,QWidget*> fields_;std::map<int,QTimer*> pending_;std::map<int,int> values_;int selected_{-1},filter_{-1};uint32_t revision_{};bool rebuilding_{};QList<LocalWallpaper> items_;uint32_t thumbnail_revision_{};int page_{},rendered_language_{-1};std::map<int,QPointer<QWidget>> tool_pages_;uint64_t image_generation_{};ImageLoader* metadata_loader_{};QString blocked_preview_path_;OnlinePage* online_{};PointerSettings* pointer_settings_{};GroupSettings* group_settings_{};
    QString t(char const* zh,char const* en) { return ui::tr(zh,en); }
    QString setting_value(int id,int value) { if(id==8){QStringList names{t("小","Small"),t("中","Medium"),t("大","Large")};return names[std::clamp(value,0,2)];}if(id==11)return QString::number(qRound(value*100./255))+"%";if(id==16)return QString::number(value)+"%";if(id==15)return QString::number(value)+" px";return QString::number(value); }
    bool flush_pending() { if(!notes_flush()||(pointer_settings_&&!pointer_settings_->flush())) return false; for(auto const& entry:pending_) if(entry.second->isActive()) { entry.second->stop();auto slider=qobject_cast<QSlider*>(fields_[entry.first]->property("slider").value<QObject*>());if(slider&&!api.change(entry.first,slider->value())) { entry.second->start();return false; } }return true; }
    QScrollArea* scroll(QWidget* body) { auto area=new QScrollArea;area->setWidgetResizable(true);area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);area->setWidget(body);return area; }
    QPushButton* night_light_button(){
        auto service=api.extension?static_cast<SystemAppearanceUiApi const*>(api.extension(7)):nullptr;
        if(service&&(service->version!=LB_APPEARANCE_ABI||service->size!=sizeof(SystemAppearanceUiApi)))service=nullptr;
        int reason=service?service->night_light():LB_NIGHTLIGHT_OS;
        auto control=button(t("Windows 护眼设置 ↗","Windows night light ↗"),[this,service]{if(service&&service->night_light()==LB_NIGHTLIGHT_AVAILABLE)QDesktopServices::openUrl(QUrl("ms-settings:nightlight"));},nullptr,"text");
        control->setProperty("nightLightAction",true);control->setEnabled(reason==LB_NIGHTLIGHT_AVAILABLE);
        char const* zh[]={"由 Windows 管理暖色强度和时间计划。","此 Windows 版本不提供系统夜间模式。","系统策略未开放夜间模式设置。","远程显示环境不提供本机夜间模式。","当前显示驱动不支持系统夜间模式。"};
        char const* en[]={"Windows manages warmth and scheduling.","This Windows version does not provide Night light.","System policy does not expose Night light settings.","Night light is unavailable in a remote display session.","The display driver does not support Night light."};
        reason=std::clamp(reason,0,4);control->setToolTip(t(zh[reason],en[reason]));control->setAccessibleDescription(control->toolTip());return control;
    }
    QWidget* settings_page(std::initializer_list<int> groups) {
        auto body=new QWidget;auto layout=new QVBoxLayout(body);layout->setContentsMargins(0,0,6,0);layout->setSpacing(12);
        char const* zh[]={"播放与系统","桌面收纳","原生任务栏","鼠标与指针","截屏与录屏","工具偏好"};char const* en[]={"Playback and system","Desktop organizer","Native taskbar","Pointer","Capture and recording","Tool preferences"};
        if(std::find(groups.begin(),groups.end(),4)!=groups.end()){auto actions=new QHBoxLayout;actions->addWidget(button(t("开始截屏 / 录屏","Capture / record"),[]{api.action(701);},nullptr,"primary"));actions->addWidget(button(t("媒体工作台","Media studio"),[this]{show_tool_page(710);}));actions->addWidget(button(t("编辑剪贴板图片","Edit clipboard image"),[]{api.action(711);}));layout->addLayout(actions);layout->addWidget(label(t("框选后标注、贴图、滚动长截图或录屏；视频预览中拖动两端逐帧裁剪。","Select a region to annotate, pin, scroll-capture or record. Drag trim handles in the video preview to edit frame by frame."),"muted",true));}
        for(int section:groups) {
            auto card=new QFrame;card->setObjectName("surface");card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);auto v=new QVBoxLayout(card);v->setContentsMargins(18,14,18,17);v->setSpacing(10);auto title=new SectionButton;title->setText(t(zh[section],en[section]));title->setAccessibleName(title->text());title->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);title->setArrowType(Qt::DownArrow);title->setCheckable(true);title->setChecked(true);v->addWidget(title);auto fields=new QWidget;auto form=new QVBoxLayout(fields);form->setContentsMargins(0,8,0,0);form->setSpacing(0);
            if(section==3){pointer_settings_=new PointerSettings;form->addWidget(pointer_settings_);}for(int id=0;section!=3;++id) { UiSetting d{};if(!api.setting(id,&d)) break;if(d.section!=section||(section==0&&(id==0||id==1))) continue;if((section==1&&id==5)||(section==2&&id==14)){auto enabled=new Toggle;enabled->setChecked(d.value);enabled->setAccessibleName(s(d.label));auto header=new QWidget;auto headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(0,0,0,0);v->removeWidget(title);headerLayout->addWidget(title,1);headerLayout->addWidget(enabled,0,Qt::AlignVCenter);v->insertWidget(0,header);fields_[id]=enabled;values_[id]=d.value;connect(enabled,&QCheckBox::toggled,this,[this,id](bool on){commit(id,on);});continue;}if(id==8){d.kind=2;d.low=0;d.high=2;}QWidget* control=nullptr;
                if(d.kind==0) { auto check=new Toggle;check->setChecked(d.value);connect(check,&QCheckBox::toggled,this,[this,id](bool on){commit(id,on);});control=check; }
                else if(d.kind==1) { auto combo=new SettingsCombo;auto choices=s(d.choices).split('|');for(int n=0;n<choices.size();++n) combo->addItem(choices[n],d.choice_values[n]);combo->setCurrentIndex(combo->findData(d.value));combo->setMinimumWidth(id==1?360:180);connect(combo,&QComboBox::currentIndexChanged,this,[this,combo,id](int){commit(id,combo->currentData().toInt());});control=combo; }
                else if(d.kind==2) { auto wrapper=new QWidget;auto line=new QHBoxLayout(wrapper);line->setContentsMargins(0,0,0,0);line->setSpacing(9);auto slider=new QSlider(Qt::Horizontal);slider->setRange(d.low,d.high);slider->setValue(d.value);slider->setMinimumWidth(130);auto value=label(setting_value(id,d.value),"muted");value->setMinimumWidth(id==8?62:38);line->addWidget(slider,1);line->addWidget(value);if(id==8){slider->setSingleStep(1);slider->setPageStep(1);slider->setTickPosition(QSlider::TicksBelow);slider->setTickInterval(1);slider->setAccessibleName(t("图标大小，小、中、大","Icon size: small, medium, large"));wrapper->setMinimumWidth(264);}auto timer=new QTimer(slider);timer->setSingleShot(true);timer->setInterval(180);pending_[id]=timer;connect(slider,&QSlider::valueChanged,this,[this,id,timer,value](int n){value->setText(setting_value(id,n));timer->start();});connect(timer,&QTimer::timeout,this,[this,id,slider]{commit(id,slider->value());});control=wrapper;wrapper->setProperty("slider",QVariant::fromValue<QObject*>(slider));wrapper->setProperty("valueLabel",QVariant::fromValue<QObject*>(value)); }
                else if(d.kind==3) { auto b=new QPushButton;set_color(b,d.value);b->setMinimumWidth(140);connect(b,&QPushButton::clicked,this,[this,id,b]{QPointer<Main> self(this);QPointer<QPushButton> buttonGuard(b);UiSetting current{};api.setting(id,&current);auto chosen=ui::dialogs::color(color(current.value),this,t("选择颜色","Choose color"));if(self&&buttonGuard&&chosen.isValid()) { set_color(b,rgb(chosen));commit(id,rgb(chosen)); }});control=b; }
                else if(d.kind==5) { auto edit=new QKeySequenceEdit(sequence(d.value));edit->setMaximumSequenceLength(1);edit->setMinimumWidth(180);connect(edit,&QKeySequenceEdit::editingFinished,this,[this,id,edit]{auto seq=edit->keySequence();if(seq.isEmpty()) return;auto combination=seq[0];int binding=native_combination(combination);if(!binding) { UiSetting prior{};api.setting(id,&prior);edit->setKeySequence(sequence(prior.value));return; }commit(id,binding);});control=edit; }
                if(control) { control->setAccessibleName(s(d.label));control->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);fields_[id]=control;values_[id]=d.value;form->addWidget(new SettingsRow(s(d.label),control)); }
            }
            if(section==0) form->addWidget(new SettingsRow(t("系统护眼模式","System night light"),night_light_button()));v->addWidget(fields);connect(title,&QToolButton::toggled,fields,[fields,title](bool open){title->setArrowType(open?Qt::DownArrow:Qt::RightArrow);fields->setVisible(open);});if(section>=1&&section<=3)title->setChecked(false);layout->addWidget(card);
        }if(std::find(groups.begin(),groups.end(),1)!=groups.end()){auto card=new QFrame;card->setObjectName("surface");card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);auto v=new QVBoxLayout(card);v->setContentsMargins(18,14,18,17);auto title=new SectionButton;title->setText(t("分组管理","Group management"));title->setChecked(false);title->setAccessibleName(title->text());title->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);title->setArrowType(Qt::RightArrow);title->setCheckable(true);v->addWidget(title);group_settings_=new GroupSettings;v->addWidget(group_settings_);group_settings_->hide();connect(title,&QToolButton::toggled,group_settings_,[this,title](bool open){title->setArrowType(open?Qt::DownArrow:Qt::RightArrow);group_settings_->setVisible(open);if(open)group_settings_->refresh();});layout->addWidget(card);}layout->addWidget(label(t("更改自动保存并生效。","Changes save and apply automatically."),"muted"));layout->addStretch();return scroll(body);
    }
    QKeySequence sequence(int raw) { int vk=raw&0xffff;auto key=keypad_key(vk);auto mods=qt_modifiers(raw>>16);if(key==Qt::Key_unknown)key=qt_key(vk);else mods|=Qt::KeypadModifier;return QKeySequence(QKeyCombination(mods,key)); }
    void set_color(QPushButton* b,int value) { auto c=color(value);QPixmap swatch(36,36);swatch.fill(Qt::transparent);QPainter p(&swatch);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#80958d"),1));p.setBrush(c);p.drawRoundedRect(QRectF(2,2,32,32),8,8);p.end();swatch.setDevicePixelRatio(2);b->setIcon(QIcon(swatch));b->setIconSize(QSize(18,18));b->setText(c.name().toUpper()); }
    void commit(int id,int value) { if(rebuilding_) return;QPointer<Main> self(this);bool changed=api.change(id,value)!=0;if(!self)return;if(changed) values_[id]=value;else values_.erase(id);refresh_state(); }
    QWidget* library() {
        auto page=new QWidget;auto v=new QVBoxLayout(page);v->setContentsMargins(0,0,0,0);v->setSpacing(12);auto tabs=new QHBoxLayout;auto stack=new QStackedWidget;library_stack_=stack;auto group=new QButtonGroup(page);for(int n=0;n<2;++n) { auto b=button(n?t("在线壁纸","Online wallpapers"):t("本地壁纸","Local wallpapers"),[this,stack,n]{suspend_preview();stack->setCurrentIndex(n);if(n==0) resume_gif();},nullptr,"pill");b->setCheckable(true);b->setChecked(n==0);library_tabs_[n]=b;group->addButton(b);tabs->addWidget(b); }tabs->addStretch();v->addLayout(tabs);stack->addWidget(local_library());online_=new OnlinePage;stack->addWidget(online_);v->addWidget(stack,1);return page;
    }
    QWidget* local_library() {
        auto page=new QWidget;auto row=new QHBoxLayout(page);row->setContentsMargins(0,0,0,0);row->setSpacing(20);auto left=new QVBoxLayout;left->setSpacing(13);preview_=new Preview;preview_->start=[this]{play_preview(true);};left->addWidget(preview_,4);auto tools=new QHBoxLayout;tools->addWidget(label(t("我的壁纸","My wallpapers"),"section"));tools->addStretch();auto group=new QButtonGroup(page);char const* zh[]={"全部","静态","GIF","视频"};char const* en[]={"All","Images","GIF","Videos"};for(int n=0;n<4;++n) { auto b=button(t(zh[n],en[n]),[this,n]{filter_=n-1;cards();},nullptr,"pill");b->setCheckable(true);b->setChecked(n-1==filter_);group->addButton(b);tools->addWidget(b); }left->addLayout(tools);search_=new QLineEdit;search_->setPlaceholderText(t("搜索壁纸名称，或拖入本地文件…","Search wallpapers, or drop local files…"));search_->setClearButtonEnabled(true);connect(search_,&QLineEdit::textChanged,this,[this]{cards();});left->addWidget(search_);local_grid_=new LocalLibraryView;local_grid_->selected=[this](int n){select(n);};local_grid_->remove=[this](int n){api.delete_item(n);refresh_state();};left->addWidget(local_grid_,5);row->addLayout(left,1);
        auto inspector=new QFrame;inspector->setObjectName("panel");inspector->setFixedWidth(248);auto v=new QVBoxLayout(inspector);v->setContentsMargins(19,21,19,19);v->setSpacing(11);v->addWidget(label(t("壁纸设置","Wallpaper settings"),"section"));meta_=label("","muted",true);v->addWidget(meta_);v->addSpacing(8);v->addWidget(label(t("显示方式","Fit"),"muted"));fit_=new SettingsCombo;UiSetting d{};api.setting(0,&d);for(int n=0;n<s(d.choices).split('|').size();++n) fit_->addItem(s(d.choices).split('|')[n],d.choice_values[n]);fit_->setCurrentIndex(fit_->findData(d.value));connect(fit_,&QComboBox::currentIndexChanged,this,[this](int){commit(0,fit_->currentData().toInt());});v->addWidget(fit_);v->addWidget(label(t("动态壁纸的显示器","Wallpaper monitor"),"muted"));monitor_=new SettingsCombo;api.setting(1,&d);for(int n=0;n<s(d.choices).split('|').size();++n) monitor_->addItem(s(d.choices).split('|')[n],d.choice_values[n]);monitor_->setCurrentIndex(monitor_->findData(d.value));connect(monitor_,&QComboBox::currentIndexChanged,this,[this](int){commit(1,monitor_->currentData().toInt());});v->addWidget(monitor_);monitor_details_=label(monitor_->currentText(),"muted",true);v->addWidget(monitor_details_);v->addSpacing(10);apply_wallpaper_=button(t("应用到桌面","Apply to desktop"),[this]{if(selected_>=0&&selected_<items_.size())blocked_preview_path_=items_[selected_].path;suspend_preview();api.action(501);},nullptr,"primary");apply_wallpaper_->setEnabled(false);v->addWidget(apply_wallpaper_);v->addWidget(button(t("停止壁纸","Stop wallpaper"),[]{api.action(502);}));v->addWidget(button(t("恢复系统壁纸","Restore original wallpaper"),[]{api.action(505);}));v->addWidget(button(t("播放 / 暂停","Play / pause"),[]{api.action(503);}));v->addWidget(button(t("声音开关","Toggle audio"),[]{api.action(504);}));v->addStretch();v->addWidget(label(t("图片、GIF、视频均保留原始文件。","Your original image, GIF and video files are preserved."),"muted",true));row->addWidget(inspector);return page;
    }
    QWidget* tools() {
        tools_=new QStackedWidget;auto home=new QWidget;auto grid=new QGridLayout(home);grid->setContentsMargins(0,0,0,0);grid->setSpacing(14);char const* zh[]={"快速启动","剪贴板历史","便签与任务","文件工作台","专注与工作区","媒体工作台"};char const* en[]={"Quick launcher","Clipboard history","Notes and tasks","File workbench","Focus and workspaces","Media studio"};char const* hintsZh[]={"搜索应用、桌面内容，或输入算式。","搜索、固定和复制文字与图片历史。","桌面便签、完成状态、归档与提醒。","文件预览、摘要与可撤销的批量改名。","专注计时与桌面布局配置。","视频预览、时间裁剪与 GIF 导出。"};char const* hintsEn[]={"Find apps and desktop items, or calculate.","Find, pin and copy text and image history.","Desktop notes, tasks, archive and reminders.","Preview, hash and safely rename files.","Focus timers and workspace layouts.","Preview, trim and export video or GIF."};
        for(int n=0;n<5;++n) { auto card=new QFrame;card->setObjectName("surface");auto v=new QVBoxLayout(card);v->setContentsMargins(20,20,20,20);v->setSpacing(12);v->addWidget(label(t(zh[n],en[n]),"section"));v->addWidget(label(t(hintsZh[n],hintsEn[n]),"muted",true));v->addStretch();v->addWidget(button(t("打开","Open"),[this,n]{if(n==0) api.action(702);else open_tool(n);}));grid->addWidget(card,n/2,n%2); }grid->setRowStretch(3,1);tools_->addWidget(home);return tools_;
    }
    void open_tool(int n) { if(n==2) { show_notes();return; }int action=n==1?707:n==3?708:n==5?710:709;if(tool_pages_[action]) { tools_->setCurrentWidget(tool_pages_[action]);return; }auto page=new QWidget;tool_pages_[action]=page;auto v=new QVBoxLayout(page);v->setContentsMargins(0,0,0,0);v->addWidget(button(t("← 工具","← Tools"),[this]{tools_->setCurrentIndex(0);},nullptr,"text"),0,Qt::AlignLeft);if(action==710)v->addWidget(scroll(new MediaStudioPage),1);else if(action==709)v->addWidget(new FocusPage,1);else if(action==708)v->addWidget(new FileWorkbench,1);else if(action==707)v->addWidget(new ClipboardPage,1);tools_->addWidget(page);tools_->setCurrentWidget(page); }
    void sync_combo(QComboBox* combo,UiSetting const& setting){QSignalBlocker block(combo);auto choices=s(setting.choices).split('|');bool changed=combo->count()!=setting.choice_count;for(int n=0;!changed&&n<setting.choice_count;++n)changed=combo->itemText(n)!=choices.value(n)||combo->itemData(n).toInt()!=setting.choice_values[n];if(changed){combo->clear();for(int n=0;n<setting.choice_count;++n)combo->addItem(choices.value(n),setting.choice_values[n]);}combo->setCurrentIndex(combo->findData(setting.value));combo->setToolTip(combo->currentText());}
    void cards() { if(local_grid_)local_grid_->set_items(items_,filter_,search_->text(),selected_); }
    void select(int index) {
        if(index<0||index>=items_.size()) return;if(local_visible())api.preview(-1,0);selected_=index;api.select_item(index);auto item=items_[index];local_grid_->set_selected(index);preview_->host->hide();preview_->title=item.title;preview_->type=item.kind==0?t("静态","Image"):item.kind==1?QString("GIF"):t("视频","Video");preview_->image=api.thumbnail?bitmap(api.thumbnail(index)):QImage{};preview_->play->setVisible(item.kind==2||(item.kind==1&&blocked_preview_path_==item.path));preview_->update();meta_->setText(item.title+"\n\n"+(item.kind==0?t("静态图片","Still image"):item.kind==1?QString("GIF"):t("视频壁纸","Video wallpaper")));
        if(blocked_preview_path_!=item.path)blocked_preview_path_.clear();load_metadata(item);
        if(item.kind==1&&local_visible()) play_preview();
    }
    void load_metadata(LocalWallpaper const& item){
        if(!metadata_loader_)metadata_loader_=new ImageLoader(this);auto generation=++image_generation_;apply_wallpaper_->setEnabled(false);apply_wallpaper_->setProperty("metadataPending",true);
        metadata_loader_->request(item.path,item.kind,{1280,720},[this,generation,item](ImageMetadata result){if(generation!=image_generation_)return;apply_wallpaper_->setProperty("metadataPending",false);apply_wallpaper_->setEnabled(result.readable);QStringList details;details<<preview_->type;if(result.dimensions.isValid())details<<QString("%1 × %2").arg(result.dimensions.width()).arg(result.dimensions.height());if(result.readable)details<<QString::number(result.bytes/1048576.,'f',2)+" MB"<<result.extension;else details<<t("原文件不存在或无法读取","Original file missing or unreadable");meta_->setText(item.title+"\n\n"+details.join("  ·  "));apply_wallpaper_->setToolTip(result.readable?QString{}:t("请重新导入原文件。","Import the original file again."));if(!result.image.isNull()){preview_->image=result.image;preview_->update();}});
    }
    bool local_visible() const { return isVisible()&&page_==0&&library_stack_&&library_stack_->currentIndex()==0; }
    void suspend_preview() { if(metadata_loader_)metadata_loader_->cancel();api.preview(-1,0);if(preview_) { preview_->host->hide();if(selected_>=0&&selected_<items_.size()) preview_->play->setVisible(items_[selected_].kind!=0); }if(online_) online_->suspend(); }
    void resume_gif() { if(local_visible()&&selected_>=0&&selected_<items_.size()){if(apply_wallpaper_->property("metadataPending").toBool())load_metadata(items_[selected_]);if(items_[selected_].kind==1)play_preview();} }
    void play_preview(bool requested=false) { if(selected_<0||selected_>=items_.size()) return;if(requested)blocked_preview_path_.clear();else if(blocked_preview_path_==items_[selected_].path)return;preview_->play->hide();preview_->host->show();api.preview(selected_,uintptr_t(preview_->host->winId())); }
    void import_files() { QPointer<Main> self(this);auto files=QFileDialog::getOpenFileNames(this,t("导入壁纸","Import wallpapers"),{},t("壁纸 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif *.mp4 *.mkv *.webm *.mov *.avi *.wmv *.ogv)","Wallpapers (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif *.mp4 *.mkv *.webm *.mov *.avi *.wmv *.ogv)"));if(!self)return;for(auto const& file:files) {if(!self)break;api.import_file(w(file));} }
    void refresh_github_icon(){if(github_){github_->setIcon(github_mark(state.dark));github_->setIconSize(QSize(19,19));}}
    QWidget* about_page() {
        auto page=new QWidget;auto outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);
        auto card=new QFrame;card->setObjectName("panel");auto layout=new QVBoxLayout(card);layout->setContentsMargins(30,30,30,30);layout->setSpacing(18);
        auto logo=new QLabel;auto pix=QIcon(s(state.directory)+"/assets/app.ico").pixmap(QSize(72,72),devicePixelRatioF());logo->setPixmap(pix);logo->setFixedSize(72,72);layout->addWidget(logo);
        layout->addWidget(label(t("轻块桌面","Light Block Desktop")+QString("  /  v" LB_VERSION_UTF8),"title",true));
        layout->addWidget(label(t("给桌面一点风景，给工作一点安静。","A little scenery for your desktop. A little calm for your work."),"section",true));
        layout->addWidget(label(t("纯属个人娱乐，有BUG随便提issues，自己用得不顺就改改","Made for personal enjoyment. Report bugs in Issues, or change what does not suit you."),"muted",true));
        auto links=new QHBoxLayout;links->setSpacing(12);github_=button(t("GitHub 项目  ↗","GitHub project  ↗"),[]{QDesktopServices::openUrl(QUrl("https://github.com/GalokPeng/light-block-desktop"));});links->addWidget(github_);links->addWidget(button(t("打开配置与日志文件夹  ↗","Open settings and logs  ↗"),[]{api.action(704);}));links->addStretch();layout->addLayout(links);refresh_github_icon();
        layout->addWidget(label(t("设置保存在本机。在线图库按需访问素材来源，无账户、无广告、无遥测。","Settings stay on your computer. Online wallpapers connect to their sources on demand. No accounts, ads or telemetry."),"muted",true));
        outer->addWidget(card);outer->addStretch();return scroll(page);
    }
    void build() {
        if(!flush_pending()) return;int prior_library_mode=library_stack_?library_stack_->currentIndex():0;auto prior_query=search_?search_->text():QString{};auto prior_online=online_?online_->view_state():OnlinePage::ViewState{};suspend_preview();++image_generation_;rebuilding_=true;pointer_settings_=nullptr;group_settings_=nullptr;fields_.clear();pending_.clear();tool_pages_.clear();values_.clear();items_.clear();local_grid_=nullptr;selected_=-1;revision_=0;auto old=centralWidget();root_=new QWidget;root_->setObjectName("root");auto main=new QHBoxLayout(root_);main->setContentsMargins(0,0,0,0);main->setSpacing(0);sidebar_=new QFrame;sidebar_->setObjectName("sidebar");sidebar_->setFixedWidth(state.english?224:184);auto side=new QVBoxLayout(sidebar_);side->setContentsMargins(16,28,16,20);side->setSpacing(7);auto brand=new QHBoxLayout;auto logo=new QLabel;QPixmap pix=QIcon(s(state.directory)+"/assets/app.ico").pixmap(QSize(34,34),devicePixelRatioF());if(!pix.isNull()) logo->setPixmap(pix);else logo->setText("◈");logo->setFixedSize(36,36);brand->addWidget(logo);auto name=new QVBoxLayout;name->setSpacing(1);name->addWidget(label(t("轻块桌面","Light Block"),"section"));name->addWidget(label(t("桌面工作台","Desktop workspace"),"muted"));brand->addLayout(name);side->addLayout(brand);side->addSpacing(33);side->addWidget(label(t("  工作空间","  WORKSPACE"),"muted"));side->addSpacing(5);char const* zh[]={"壁纸","美化","截屏","工具","设置","关于"};char const* en[]={"Wallpapers","Appearance","Capture","Tools","Settings","About"};auto navigation=new QButtonGroup(sidebar_);for(int n=0;n<6;++n) { nav_[n]=button(t(zh[n],en[n]),[this,n]{navigate(n);},nullptr,"nav");nav_[n]->setIcon(icon(n,!state.dark));nav_[n]->setIconSize(QSize(18,18));nav_[n]->setCheckable(true);navigation->addButton(nav_[n]);side->addWidget(nav_[n]); }side->addStretch();auto local=update_button(this);side->addWidget(local);main->addWidget(sidebar_);auto content=new QWidget;auto outer=new QVBoxLayout(content);outer->setContentsMargins(28,24,28,16);outer->setSpacing(18);auto top=new QHBoxLayout;auto titles=new QVBoxLayout;titles->setSpacing(6);eyebrow_=label(t("给桌面留一点安静","A QUIETER DESKTOP"),"eyebrow");heading_=label("","title",true);heading_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);titles->addWidget(eyebrow_);titles->addWidget(heading_);top->addLayout(titles);top->addStretch();theme_=button("",[]{api.action(601);},nullptr,"compactAction");theme_->setFixedSize(38,36);theme_->setIcon(icon(6,!state.dark));theme_->setIconSize(QSize(20,20));theme_->setToolTip(t("切换明暗主题","Toggle theme"));theme_->setAccessibleName(theme_->toolTip());top->addWidget(theme_);language_=button(state.english?"中":"EN",[]{api.action(602);},nullptr,"compactAction");language_->setFixedSize(47,36);language_->setAccessibleName(t("界面语言","Interface language"));language_->setToolTip(t("切换为 English","Switch to 简体中文"));top->addWidget(language_);import_=button(t("＋  导入壁纸","＋  Import"),[]{},nullptr,"primary");import_->setMinimumWidth(124);top->addWidget(import_);outer->addLayout(top);pages_=new QStackedWidget;pages_->addWidget(library());pages_->addWidget(settings_page({1,2,3}));pages_->addWidget(settings_page({4}));pages_->addWidget(tools());pages_->addWidget(settings_page({0,5}));pages_->addWidget(about_page());outer->addWidget(pages_,1);status_=label("","muted",true);outer->addWidget(status_);main->addWidget(content,1);setCentralWidget(root_);if(old) old->deleteLater();rebuilding_=false;rendered_language_=state.english;search_->setText(prior_query);online_->restore_view(prior_online);library_stack_->setCurrentIndex(prior_library_mode);library_tabs_[prior_library_mode]->setChecked(true);navigate(std::min(page_,5));refresh_state();
    }
    void style() { QPalette palette;palette.setColor(QPalette::Window,QColor(state.dark?"#11151c":"#f3f6f4"));palette.setColor(QPalette::WindowText,QColor(state.dark?"#e9edf0":"#26343b"));palette.setColor(QPalette::Text,palette.color(QPalette::WindowText));palette.setColor(QPalette::ButtonText,palette.color(QPalette::WindowText));palette.setColor(QPalette::Base,QColor(state.dark?"#121a24":"#ffffff"));palette.setColor(QPalette::Button,QColor(state.dark?"#242d39":"#f6f8f7"));palette.setColor(QPalette::Highlight,QColor("#426855"));palette.setColor(QPalette::HighlightedText,QColor("#f4faf7"));qApp->setPalette(palette);QFile sheet(s(state.directory)+(state.dark?"/ui/dark.qss":"/ui/light.qss"));if(sheet.open(QIODevice::ReadOnly)) setStyleSheet(QString::fromUtf8(sheet.readAll())+QString("QSlider::groove:horizontal {height:4px;background:#354151;border-radius:2px;} QSlider::handle:horizontal {width:14px;margin:-5px 0;background:#a6e4ca;border-radius:7px;} QPlainTextEdit {background:%1;color:%2;border:1px solid %3;border-radius:8px;padding:10px;} QKeySequenceEdit {background:%1;color:%2;border:1px solid %3;border-radius:7px;padding:7px;} ").arg(state.dark?"#172330":"#ffffff",state.dark?"#e9edf0":"#26343b",state.dark?"#344253":"#c5d5cd"));BOOL dark=state.dark;HWND hwnd=reinterpret_cast<HWND>(winId());DwmSetWindowAttribute(hwnd,20,&dark,sizeof dark);COLORREF background=state.dark?RGB(17,21,28):RGB(243,246,244);DwmSetWindowAttribute(hwnd,35,&background,sizeof background); }
public:
    bool prepare_exit(bool closing) { if(!flush_pending()) return false;if(closing) {setEnabled(false);refresh_.stop();}return true; }
    void cancel_exit() {setEnabled(true);if(isVisible()) refresh_.start();}
#include "snapshot.inc"
    Main() { setWindowTitle(s(state.english?LB_PRODUCT_ENGLISH:LB_PRODUCT_NAME));setWindowIcon(QIcon(s(state.directory)+"/assets/app.ico"));setMinimumSize(960,620);auto available=QGuiApplication::primaryScreen()?QGuiApplication::primaryScreen()->availableGeometry().size():QSize(1250,850);resize(QSize(1250,850).boundedTo(available-QSize(40,60)));setAcceptDrops(true);build();style();refresh_.setInterval(200);connect(&refresh_,&QTimer::timeout,this,[this]{refresh_state();});refresh_.start(); }
    void navigate(int page) { if(!flush_pending()) return;if(page!=0) suspend_preview();page_=page;char const* headingsZh[]={"壁纸","美化","截屏与录屏","工具","设置","关于"};char const* headingsEn[]={"Wallpapers","Appearance","Capture and recording","Tools","Settings","About"};heading_->setText(t(headingsZh[page],headingsEn[page]));setWindowTitle(s(state.english?LB_PRODUCT_ENGLISH:LB_PRODUCT_NAME));pages_->setCurrentIndex(page);nav_[page]->setChecked(true);if(page==0) resume_gif();if(page==3) tools_->setCurrentIndex(0);import_->setVisible(page==0||page==3);import_->setText(page==3?t("＋  新建便签","＋  New note"):t("＋  导入壁纸","＋  Import"));disconnect(import_,nullptr,this,nullptr);connect(import_,&QPushButton::clicked,this,[this]{if(page_==3) api.action(700);else import_files();}); }
    void show_tool_page(int action) { if(action==706){navigate(0);suspend_preview();library_stack_->setCurrentIndex(1);library_tabs_[1]->setChecked(true);}else{navigate(3);open_tool(action==707?1:action==708?3:action==710?5:4);}show_page(-1); }
    void show_page(int page) { if(page>=0&&page<6) navigate(page);showNormal();ShowWindow(reinterpret_cast<HWND>(winId()),SW_RESTORE);raise();activateWindow();refresh_.start();refresh_state(); }
    uintptr_t preview_host() { return uintptr_t(preview_->host->winId()); }
    void show_notes() { navigate(3);auto page=notes_page();tools_->addWidget(page);tools_->setCurrentWidget(page); }
    void refresh_state() {
        if(rebuilding_) return;auto old=state;api.state(&state);if(state.english!=rendered_language_) { build();style();return; }if(state.dark!=old.dark) { style();for(int n=0;n<6;++n) nav_[n]->setIcon(icon(n,!state.dark));theme_->setIcon(icon(6,!state.dark));refresh_github_icon(); }
        if(pointer_settings_)pointer_settings_->refresh();status_->setText(s(state.status));if(online_) online_->refresh(state);for(auto const& pair:fields_) { int id=pair.first;auto control=pair.second;UiSetting d{};if(!api.setting(id,&d)) continue;if(id==1){sync_combo(static_cast<QComboBox*>(control),d);values_[id]=d.value;continue;}if(pending_.contains(id)&&pending_[id]->isActive()) continue;if(values_.contains(id)&&values_[id]==d.value) continue;QSignalBlocker block(control);if(id==8)d.kind=2;if(d.kind==0) static_cast<QCheckBox*>(control)->setChecked(d.value);else if(d.kind==1) { auto combo=static_cast<QComboBox*>(control);combo->setCurrentIndex(combo->findData(d.value)); }else if(d.kind==2) { auto slider=qobject_cast<QSlider*>(control->property("slider").value<QObject*>());if(slider) { QSignalBlocker b(slider);slider->setValue(d.value);if(auto label=qobject_cast<QLabel*>(control->property("valueLabel").value<QObject*>())) label->setText(setting_value(id,d.value)); } }else if(d.kind==3) set_color(static_cast<QPushButton*>(control),d.value);else if(d.kind==5) static_cast<QKeySequenceEdit*>(control)->setKeySequence(sequence(d.value));values_[id]=d.value; }
        for(auto pair:{std::pair{fit_,0},std::pair{monitor_,1}}) { UiSetting d{};api.setting(pair.second,&d);sync_combo(pair.first,d);if(pair.second==1&&monitor_details_)monitor_details_->setText(pair.first->currentText()); }
        if(thumbnail_revision_!=state.thumbnail_revision){thumbnail_revision_=state.thumbnail_revision;if(local_grid_)local_grid_->thumbnails_changed();if(selected_>=0&&selected_<items_.size()&&items_[selected_].kind!=0&&preview_->host->isHidden()&&api.thumbnail){auto image=bitmap(api.thumbnail(selected_));if(!image.isNull()){preview_->image=image;preview_->update();}}}
        if(revision_!=state.library_revision||items_.size()!=state.count) { QString prior=selected_>=0&&selected_<items_.size()?items_[selected_].path:QString{};revision_=state.library_revision;items_.clear();for(int n=0;n<state.count;++n) { UiItem item{};if(api.item(n,&item)) items_.push_back(LocalWallpaper::from(item,n)); }int target=items_.isEmpty()?-1:std::clamp(state.selected,0,int(items_.size())-1);bool changed=target<0||prior!=items_[target].path;selected_=target;cards();if(changed&&target>=0) select(target);else if(target<0) { ++image_generation_;if(local_visible())api.preview(-1,0);preview_->host->hide();preview_->play->hide();preview_->image={};preview_->title.clear();preview_->update();meta_->clear();apply_wallpaper_->setProperty("metadataPending",false);apply_wallpaper_->setEnabled(false);apply_wallpaper_->setToolTip({}); } }else if(state.selected!=selected_&&state.selected>=0&&state.selected<items_.size()) select(state.selected);
    }
    void closeEvent(QCloseEvent* event) override { if(!flush_pending()) { event->ignore();return; }hide();refresh_.stop();event->ignore();QTimer::singleShot(0,qApp,[self=this]{if(main_window.get()==self) main_window.reset();}); }
    void showEvent(QShowEvent*) override { resume_gif(); }
    void hideEvent(QHideEvent*) override { suspend_preview();refresh_.stop(); }
    void dragEnterEvent(QDragEnterEvent* e) override { if(e->mimeData()->hasUrls()) e->acceptProposedAction(); }
    void dropEvent(QDropEvent* e) override { for(auto const& url:e->mimeData()->urls()) if(url.isLocalFile()) api.import_file(w(url.toLocalFile())); }
};
std::unique_ptr<QApplication> application;std::unique_ptr<Main> main_window;
// Qt top-level windows can receive the session notification before the native
// controller. Route both through the same idempotent cleanup path, instead of
// letting Qt quit its event loop before desktop restoration has started.
class SessionEvents final:public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(QByteArray const&,void* raw,qintptr* result) override {
        auto message=static_cast<MSG*>(raw);
        if(message->message==WM_QUERYENDSESSION) {*result=api.session_end(-1);return true;}
        if(message->message==WM_ENDSESSION) {*result=api.session_end(message->wParam?1:0);return true;}
        return false;
    }
};
SessionEvents session_events;
int initialize(UiApi const* supplied,int visible) { if(!supplied||supplied->version!=LB_UI_ABI||supplied->size!=sizeof(UiApi)) return 0;api=*supplied;api.state(&state);qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFile::encodeName(s(state.directory)+"/qt-plugins/platforms"));if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","windows:fontengine=gdi");qInstallMessageHandler([](QtMsgType,QMessageLogContext const&,QString const& message){QFile log(s(state.data)+"/qt-ui.log");if(log.open(QIODevice::WriteOnly|QIODevice::Append)) { log.write(message.toUtf8());log.write("\n"); }});static int argc=1;static char name[]="LightBlockDesktop";static char* argv[]={name,nullptr};application=std::make_unique<QApplication>(argc,argv);application->setFont(QFont("Microsoft YaHei UI",9));application->setQuitOnLastWindowClosed(false);application->installNativeEventFilter(&session_events);application->setStyle("Fusion");application->addLibraryPath(s(state.directory)+"/qt-plugins");if(visible) { main_window=std::make_unique<Main>();main_window->show();ShowWindow(reinterpret_cast<HWND>(main_window->winId()),SW_RESTORE);QTimer::singleShot(1000,qApp,[]{updates().check();}); }return 1; }
int snapshots(wchar_t const* directory) { if(!main_window) main_window=std::make_unique<Main>();bool ok=main_window->snapshots(s(directory));QTimer::singleShot(0,qApp,[]{api.action(900);});return ok; }
int update_tests(wchar_t const* directory) { bool ok=UpdateService::tests(s(directory));QTimer::singleShot(0,qApp,[]{api.action(900);});return ok?1:0; }
int prepare_exit(int closing){if(!notes_flush_all())return 0;return !main_window||main_window->prepare_exit(closing!=0);}
void update_exit_failed(){if(main_window)main_window->cancel_exit();if(update_service)update_service->quit_failed();}
int run() { int code=application->exec();notes_shutdown(1);main_window.reset();update_service.reset();application.reset();return code; }
void show(int page) { if(!main_window) { api.state(&state);main_window=std::make_unique<Main>(); }main_window->show_page(page); }
void hide() {
    if(auto popup=QApplication::activePopupWidget()) popup->hide();
    if(!main_window) return;
    auto hwnd=HWND(main_window->winId());BOOL previous=FALSE,disabled=TRUE;
    DwmGetWindowAttribute(hwnd,DWMWA_TRANSITIONS_FORCEDISABLED,&previous,sizeof previous);
    DwmSetWindowAttribute(hwnd,DWMWA_TRANSITIONS_FORCEDISABLED,&disabled,sizeof disabled);
    main_window->hide();
    QTimer::singleShot(350,main_window.get(),[hwnd,previous]{if(IsWindow(hwnd))DwmSetWindowAttribute(hwnd,DWMWA_TRANSITIONS_FORCEDISABLED,&previous,sizeof previous);});
}
int capture_hidden(){return (!main_window||!main_window->isVisible())&&!QApplication::activePopupWidget();}
uintptr_t preview_host() { return main_window?main_window->preview_host():0; }
void show_tool(int action) {if(!main_window){api.state(&state);main_window=std::make_unique<Main>();}main_window->show_tool_page(action);}
void notes_manager() { if(!main_window) { api.state(&state);main_window=std::make_unique<Main>(); }main_window->show_notes();main_window->show_page(-1); }
}
extern "C" __declspec(dllexport) UiExports const* LightBlockUiExports() {
    static UiExports exports{LB_UI_ABI,sizeof(UiExports),ui::initialize,ui::run,ui::show,ui::hide,ui::preview_host,ui::note_open,ui::note_close,ui::notes_shutdown,ui::notes_manager,ui::snapshots,ui::show_launcher,ui::show_tool,ui::update_tests,ui::update_exit_failed,ui::prepare_exit,ui::capture_hidden};return &exports;
}
