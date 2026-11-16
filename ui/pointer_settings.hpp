#pragma once
#include "ui.hpp"
#include "dialogs.hpp"
#include "settings_widgets.hpp"
#include <array>
#include <cmath>

namespace ui {
namespace pointer_preview {
inline QImage tint(QImage image,QColor ink) {
    if(image.isNull()||ink==QColor("#d9f2ed")) return image;
    image=image.convertToFormat(QImage::Format_ARGB32);
    // Match cursor_art.cpp exactly, including its dark-color contrast edge and
    // unmodified translucent shadow. This previews the cursor the helper uses.
    const bool light=ink.red()*299+ink.green()*587+ink.blue()*114>125000;
    const int edge[]={light?21:243,light?33:249,light?44:255};
    const int target[]={ink.red(),ink.green(),ink.blue()};
    const int old_edge[]={21,33,44},old_ink[]={217,242,237};
    double norm=0;for(int c=0;c<3;++c) norm+=std::pow(old_ink[c]-old_edge[c],2);
    for(int y=0;y<image.height();++y) {
        auto row=reinterpret_cast<QRgb*>(image.scanLine(y));
        for(int x=0;x<image.width();++x) {
            QRgb p=row[x];int alpha=qAlpha(p);if(!alpha)continue;
            int rgb[]={qRed(p),qGreen(p),qBlue(p)};
            if(alpha<=120&&std::abs(rgb[0]-15)<3&&std::abs(rgb[1]-24)<3&&std::abs(rgb[2]-36)<3)continue;
            double mix=0;for(int c=0;c<3;++c)mix+=(rgb[c]-old_edge[c])*(old_ink[c]-old_edge[c]);mix=std::clamp(mix/norm,0.,1.);
            for(int c=0;c<3;++c)rgb[c]=std::clamp(int(std::lround(edge[c]*(1-mix)+target[c]*mix)),0,255);
            row[x]=qRgba(rgb[0],rgb[1],rgb[2],alpha);
        }
    }
    return image;
}
inline QIcon system_icon() {
    QPixmap image(84,84);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#80a995"),5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawArc(QRectF(20,20,44,44),45*16,285*16);p.drawPolyline(QPolygonF{{68,18},{68,34},{52,34}});p.end();return QIcon(image);
}
class Toggle final:public QAbstractButton {
public:
    explicit Toggle(QWidget* parent=nullptr):QAbstractButton(parent) {setCheckable(true);setFixedSize(38,23);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);}
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(hasFocus()?QPen(QColor("#ceeedd"),1):QPen(Qt::NoPen));p.setBrush(QColor(isChecked()?"#a6e4ca":"#3b4756"));p.drawRoundedRect(QRectF(1,2,36,20),10,10);p.setPen(Qt::NoPen);p.setBrush(QColor(isChecked()?"#18392b":"#b3beca"));p.drawEllipse(QRectF(isChecked()?20:4,5,14,14));
    }
};
}

class PointerSettings:public QWidget {
    std::array<QToolButton*,4> themes_{};
    std::array<QImage,3> source_,tinted_;
    std::array<int,6> values_{};
    QLabel *status_{},*size_label_{},*effect_hint_{};
    QPushButton *cursor_color_{},*particle_color_{};
    QSlider* size_{};
    pointer_preview::Toggle* particles_{};
    QComboBox* particle_style_{};
    QTimer size_save_;
    bool pending_size_{},updating_{};
    QString error_;
    int preview_color_{-1};
    bool presented_{};
    int presented_size_{},presented_language_{};
    QString presented_error_;
    unsigned presentation_count_{};

    std::array<int,6> read_values()const {
        std::array<int,6> result{0,100,0xedf2d9,0,0,0xcae4a6};
        for(int n=0;n<6;++n){UiSetting field{};if(api.setting(18+n,&field))result[n]=field.value;}
        return result;
    }
    QString t(char const* zh,char const* en)const{return ui::tr(zh,en);}
    void color_button(QPushButton* button,int value) {
        QColor shade=ui::color(value);QPixmap swatch(32,32);swatch.fill(Qt::transparent);QPainter p(&swatch);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#80958d"),1));p.setBrush(shade);p.drawRoundedRect(QRectF(2,2,28,28),7,7);p.end();swatch.setDevicePixelRatio(2);button->setIcon(QIcon(swatch));button->setIconSize(QSize(16,16));button->setText(shade.name().toUpper());button->setStyleSheet(QString("QPushButton {border:2px solid %1;}").arg(shade.name()));
    }
    void apply_values(std::array<int,6> const& next,bool preserve_pending,bool force=false) {
        const int displayed_size=preserve_pending&&(pending_size_||size_->isSliderDown())?size_->value():std::clamp(next[1],75,200);
        // The host polls every 200 ms. Stable state must not recreate swatches,
        // restyle cards or schedule layout work while the app is idle.
        if(!force&&preserve_pending&&presented_&&next==values_&&displayed_size==presented_size_&&presented_language_==state.english&&presented_error_==error_)return;
        values_=next;updating_=true;
        for(int n=0;n<4;++n){QSignalBlocker block(themes_[n]);themes_[n]->setChecked(n==std::clamp(next[0],0,3));}
        if(!preserve_pending||(!pending_size_&&!size_->isSliderDown())){QSignalBlocker block(size_);size_->setValue(std::clamp(next[1],75,200));}
        size_label_->setText(QString::number(size_->value())+"%");
        {QSignalBlocker block(particles_);particles_->setChecked(next[3]!=0);}
        {QSignalBlocker block(particle_style_);particle_style_->setCurrentIndex(std::clamp(next[4],0,2));}
        color_button(cursor_color_,next[2]);color_button(particle_color_,next[5]);
        if(preview_color_!=next[2]) {
            for(int n=0;n<3;++n){tinted_[n]=pointer_preview::tint(source_[n],ui::color(next[2]));themes_[n+1]->setIcon(QIcon(QPixmap::fromImage(tinted_[n])));}
            preview_color_=next[2];
        }
        static char const* zh[]={"点点：短尾迹逐渐变小、淡出。","飘落：细微荧光轻柔下落，淡入后逐渐消散。","光芒：沿当前指针的外轮廓流动，停下后柔和消散。"};
        static char const* en[]={"Dots: a short trail shrinks and fades away.","Fall: tiny glowing particles drift down and gently dissolve.","Glow: light follows the cursor outline, fading softly when still."};
        int effect=std::clamp(next[4],0,2);effect_hint_->setText(t(zh[effect],en[effect]));
        status_->setText(!error_.isEmpty()?error_:next[0]==0?t("当前使用 Windows 系统指针。","Currently using the Windows cursor scheme."):t("已选择美化指针；退出时恢复系统方案。","Custom cursor selected; the system scheme is restored on exit."));
        updating_=false;presented_=true;presented_size_=size_->value();presented_language_=state.english;presented_error_=error_;++presentation_count_;
    }
    bool change(int id,int value) {
        if(updating_)return true;bool ok=api.change(id,value)!=0;
        if(ok)error_.clear();else{UiState current{};api.state(&current);error_=s(current.status);if(error_.isEmpty())error_=t("设置未能应用，已保留原设置。","The setting could not be applied. The previous value was kept.");}
        // A rejected user click still needs to restore the touched widget even
        // when all six backend values are unchanged.
        apply_values(read_values(),true,true);return ok;
    }
    void choose_color(int id) {
        UiSetting current{};if(!api.setting(id,&current))return;
        QPointer<PointerSettings> self(this);QColor value=ui::dialogs::color(ui::color(current.value),this,id==20?t("指针颜色","Cursor color"):t("粒子颜色","Particle color"));
        if(self&&value.isValid())change(id,ui::rgb(value));
    }
    void field(QVBoxLayout* layout,QString title,QWidget* value) {
        layout->addWidget(new SettingsRow(std::move(title),value));
    }
public:
    explicit PointerSettings(QWidget* parent=nullptr):QWidget(parent) {
        setObjectName("pointerSettings");setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);
        auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(12);
        layout->addWidget(label(t("三种不同轮廓，覆盖文本、链接、缩放与忙碌动画；退出时恢复系统指针。","Three distinct shapes cover text, links, resize and busy states; the system cursors return on exit."),"muted",true));
        char const* keys[]={"stream","outline","pixel"};for(int n=0;n<3;++n){QImageReader reader(s(state.directory)+"/assets/cursors/"+keys[n]+"/preview.png");source_[n]=reader.read();}
        auto grid=new QGridLayout;grid->setSpacing(10);auto group=new QButtonGroup(this);
        char const* zh[]={"系统方案","流线 · 圆润实心","棱镜 · 几何镂空","像素 · 阶梯复古"};char const* en[]={"System scheme","Stream · Rounded solid","Outline · Geometric hollow","Pixel · Retro steps"};
        for(int n=0;n<4;++n){auto b=new QToolButton;themes_[n]=b;b->setObjectName("cursorTheme");b->setCheckable(true);b->setMinimumHeight(84);b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);b->setText(t(zh[n],en[n]));b->setAccessibleName(t("鼠标主题 · ","Cursor theme · ")+b->text());b->setCursor(Qt::PointingHandCursor);b->setIconSize(QSize(180,42));b->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);if(n==0)b->setIcon(pointer_preview::system_icon());group->addButton(b,n);grid->addWidget(b,n/2,n%2);connect(b,&QToolButton::clicked,this,[this,n]{if(flush())change(18,n);else refresh();});}
        grid->setColumnStretch(0,1);grid->setColumnStretch(1,1);layout->addLayout(grid);
        status_=label({},"muted",true);layout->addWidget(status_);
        cursor_color_=button({},[this]{choose_color(20);});cursor_color_->setMinimumWidth(120);cursor_color_->setAccessibleName(t("指针颜色","Cursor color"));field(layout,t("指针颜色","Cursor color"),cursor_color_);
        size_label_=label({},"muted");size_label_->setMinimumWidth(46);size_=new QSlider(Qt::Horizontal);size_->setRange(75,200);size_->setSingleStep(5);size_->setPageStep(25);size_->setAccessibleName(t("指针大小","Cursor size"));auto sizeControls=new QWidget;auto sizeLine=new QHBoxLayout(sizeControls);sizeLine->setContentsMargins(0,0,0,0);sizeLine->setSpacing(10);sizeLine->addWidget(size_,1);sizeLine->addWidget(size_label_);field(layout,t("指针大小","Cursor size"),sizeControls);
        size_save_.setSingleShot(true);size_save_.setInterval(180);connect(&size_save_,&QTimer::timeout,this,[this]{flush();});connect(size_,&QSlider::valueChanged,this,[this](int value){if(updating_)return;size_label_->setText(QString::number(value)+"%");pending_size_=true;size_save_.start();});
        layout->addWidget(label(t("大小按系统 DPI 缩放；系统方案下先保存，选择美化指针后生效。","Size follows system DPI. While using system cursors, the value is saved for the next custom theme."),"muted",true));
        particles_=new pointer_preview::Toggle;particles_->setAccessibleName(t("鼠标粒子特效","Pointer particles"));field(layout,t("鼠标粒子特效","Pointer particles"),particles_);connect(particles_,&QAbstractButton::toggled,this,[this](bool enabled){if(!updating_)change(21,enabled);});
        particle_style_=new SettingsCombo;particle_style_->setEditable(false);particle_style_->addItems({t("点点","Dots"),t("飘落","Fall"),t("光芒","Glow")});particle_style_->setMinimumWidth(140);particle_style_->setAccessibleName(t("粒子样式","Particle style"));field(layout,t("粒子样式","Particle style"),particle_style_);connect(particle_style_,&QComboBox::currentIndexChanged,this,[this](int value){if(!updating_)change(22,value);});particle_color_=button({},[this]{choose_color(23);});particle_color_->setMinimumWidth(120);particle_color_->setAccessibleName(t("粒子颜色","Particle color"));field(layout,t("粒子颜色","Particle color"),particle_color_);
        effect_hint_=label({},"muted",true);layout->addWidget(effect_hint_);layout->addWidget(label(t("指针与粒子可自由组合、独立调色。停止移动后自然淡出并停绘，关闭后释放窗口与监听。","Combine cursors and particles freely, with separate colors. Effects fade and stop drawing when idle; disabling them releases their window and listener."),"muted",true));
        refresh();
    }
    bool flush() {if(!pending_size_)return true;size_save_.stop();pending_size_=false;return change(19,size_->value());}
    void refresh() {apply_values(read_values(),true);}
    bool fixture_tests(QString const& directory) {
        // Render fixtures through local presentation data only. Never click a
        // theme or call api.change: test snapshots cannot change system cursors.
        if(pending_size_||size_save_.isActive())return false;
        auto original=read_values();QString previous_error=error_;bool ok=QDir().mkpath(directory);std::array<int,6> fixture{0,100,0xedf2d9,0,0,0xcae4a6};
        QJsonObject report;bool sources=true,roles=true,alpha=true,changed=true;
        for(int n=0;n<3;++n){sources=sources&&!source_[n].isNull();roles=roles&&source_[n].size()==QSize(300,70);auto colored=pointer_preview::tint(source_[n],QColor("#8159f2"));changed=changed&&colored!=source_[n];for(int y=0;y<colored.height();++y)for(int x=0;x<colored.width();++x)alpha=alpha&&qAlpha(colored.pixel(x,y))==qAlpha(source_[n].pixel(x,y));}
        report["bundled_preview_assets"]=sources;report["five_roles_per_theme"]=roles;report["tint_preserves_alpha"]=alpha;report["tint_changes_rgb"]=changed;
        apply_values(original,false);auto count=presentation_count_;apply_values(original,true);apply_values(original,true);report["idle_refresh_does_not_restyle"]=presentation_count_==count;
        {QSignalBlocker block(size_);size_->setValue(original[1]==125?150:125);}auto draft=size_->value();pending_size_=true;apply_values(original,true);report["refresh_preserves_size_draft"]=size_->value()==draft;pending_size_=false;apply_values(original,false);
        error_=t("测试提示","Test notice");apply_values(original,true);report["changed_error_updates_status"]=status_->text()==error_;error_=previous_error;apply_values(original,false);
        // Never resize the live page inside the scroll area's layout: QWidget::render
        // can activate that parent layout and enlarge it after image allocation.
        // A detached, fixed-size presentation copy has the same effective theme.
        PointerSettings canvas;canvas.setPalette(palette());canvas.setFont(font());
        QString inherited=qApp->styleSheet();QList<QWidget*> ancestors;for(QWidget* parent=this;parent;parent=parent->parentWidget())ancestors.prepend(parent);for(auto parent:ancestors)inherited+="\n"+parent->styleSheet();canvas.setStyleSheet(inherited);canvas.ensurePolished();
        const int canvas_width=std::max(640,canvas.minimumSizeHint().width());canvas.setFixedWidth(canvas_width);bool complete_cards=true,stable_extent=true;
        for(int theme=0;theme<4;++theme){
            fixture[0]=theme;fixture[1]=theme==1?75:theme==3?200:100;fixture[2]=theme==2?0xf25981:0xedf2d9;fixture[4]=std::max(0,theme-1);canvas.apply_values(fixture,false);
            canvas.layout()->activate();int canvas_height=std::max(canvas.minimumSizeHint().height(),canvas.layout()->hasHeightForWidth()?canvas.layout()->heightForWidth(canvas_width):canvas.sizeHint().height());canvas.setFixedHeight(canvas_height);canvas.layout()->activate();
            const QSize extent=canvas.size();const qreal ratio=canvas.devicePixelRatioF();QImage image(QSize(qCeil(extent.width()*ratio),qCeil(extent.height()*ratio)),QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(ratio);image.fill(Qt::transparent);canvas.render(&image,QPoint(),QRegion(QRect(QPoint(),extent)));
            stable_extent=stable_extent&&canvas.size()==extent&&image.width()>=qCeil(canvas.width()*ratio)&&image.height()>=qCeil(canvas.height()*ratio);
            for(auto card:canvas.themes_)complete_cards=complete_cards&&canvas.rect().contains(QRect(card->mapTo(&canvas,QPoint()),card->size()));
            ok=image.save(directory+QString("/pointer-theme-%1.png").arg(theme))&&ok;ok=canvas.themes_[theme]->isChecked()&&canvas.size_->value()==fixture[1]&&ok;
        }
        report["detached_fixture_complete_cards"]=complete_cards&&canvas.parentWidget()==nullptr;report["fixture_render_extent_stable"]=stable_extent;
        report["four_theme_renders"]=ok;report["noneditable_style_choice"]=!particle_style_->isEditable();report["settings_unchanged"]=read_values()==original;report["visual_fixture_only"]=true;
        error_=previous_error;apply_values(original,false);for(auto it=report.begin();it!=report.end();++it)ok=ok&&it.value().toBool();report["passed"]=ok;QSaveFile file(directory+"/pointer-ui-checks.json");if(!file.open(QIODevice::WriteOnly))return false;auto bytes=QJsonDocument(report).toJson();return file.write(bytes)==bytes.size()&&file.commit()&&ok;
    }
};
}
