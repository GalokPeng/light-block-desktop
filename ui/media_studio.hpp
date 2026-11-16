#pragma once
#include "ui.hpp"
#include "dialogs.hpp"
#include "settings_widgets.hpp"
#include "../src/media_studio_ui_api.h"
#include <algorithm>

namespace ui {
class MediaStudioPage final:public QWidget {
    UiStudioApi const* backend_{};UiStudioState snapshot_{};QTimer poll_;
    QLineEdit* source_{};QLabel *details_{},*message_{};QWidget *host_{},*form_{};
    QPushButton *browse_{},*play_{},*export_{},*cancel_{};QProgressBar* progress_{};
    QComboBox *format_{},*gif_width_{},*palette_{};QSpinBox *crop_[4]{},*fps_{};
    QDoubleSpinBox *first_{},*last_{};QString loaded_;bool playing_{};
    struct Draft {QString source;double first{},last{};int crop[4]{},format{},fps{12},width{},palette{};};
    static inline std::unique_ptr<Draft> draft_;
    QString t(char const* zh,char const* en){return ui::tr(zh,en);}
    void remember(){if(loaded_.isEmpty())return;auto d=std::make_unique<Draft>();d->source=loaded_;d->first=first_->value();d->last=last_->value();for(int n=0;n<4;++n)d->crop[n]=crop_[n]->value();d->format=format_->currentIndex();d->fps=fps_->value();d->width=gif_width_->currentIndex();d->palette=palette_->currentIndex();draft_=std::move(d);}
    void stop_preview(){if(playing_){api.preview(-1,0);playing_=false;host_->hide();play_->setText(t("播放预览","Play preview"));}}
    void choose(){QPointer<MediaStudioPage> self(this);auto path=ui::dialogs::open(this,t("选择视频或 GIF 素材","Choose video or GIF"),{},"Video / GIF (*.mp4 *.mkv *.mov *.webm *.avi *.m4v *.gif);;All files (*)");if(!self||path.isEmpty())return;stop_preview();if(!backend_||!backend_->open(w(path)))message_->setText(t("无法打开素材，请结束当前任务后重试。","Cannot open this file. Finish the current job and try again."));refresh();}
    void export_media(){
        if(!backend_)return;QPointer<MediaStudioPage> self(this);QString suffix=format_->currentIndex()==1?".gif":format_->currentIndex()==2?".mkv":".mp4";QString suggestion=QFileInfo(loaded_).absolutePath()+"/"+QFileInfo(loaded_).completeBaseName()+"_LightBlock"+suffix;
        auto path=ui::dialogs::save(this,t("导出新文件","Export new file"),suggestion,"Media (*"+suffix+")",nullptr,QFileDialog::DontConfirmOverwrite);if(!self||path.isEmpty())return;if(!path.endsWith(suffix,Qt::CaseInsensitive))path+=suffix;
        if(QFileInfo::exists(path)){message_->setText(t("输出文件已存在，请使用新名称。原素材不会被覆盖。","The output already exists. Choose a new name; the source will not be overwritten."));return;}
        UiStudioExport request{};request.first_ms=qRound64(first_->value()*1000);request.last_ms=qRound64(last_->value()*1000);request.x=crop_[0]->value();request.y=crop_[1]->value();request.width=crop_[2]->value();request.height=crop_[3]->value();request.format=format_->currentIndex();request.fps=fps_->value();request.gif_width=gif_width_->currentData().toInt();request.gif_fast=palette_->currentIndex()==0;
        if(path.size()>=4096){message_->setText(t("文件路径过长。","The path is too long."));return;}memcpy(request.destination,path.utf16(),size_t(path.size()+1)*sizeof(wchar_t));
        remember();if(!backend_->export_file(&request))message_->setText(t("请检查时间范围、裁剪区域及输出位置；导出时使用新的文件名。","Check the time range, crop and output location. Use a new output filename."));else refresh();
    }
    void refresh(){
        if(!backend_)return;UiStudioState next{};backend_->state(&next);bool busy=next.phase==1||next.phase==3,ready=next.width>0&&(next.phase==2||next.phase==4);
        browse_->setEnabled(!busy);form_->setEnabled(ready);export_->setEnabled(ready);play_->setEnabled(ready);cancel_->setEnabled(busy);progress_->setRange(next.phase==1?0:0,next.phase==1?0:100);progress_->setValue(next.progress);
        if(next.serial!=snapshot_.serial){source_->setText(s(next.source));message_->setText(s(next.message));if(next.phase==4)message_->setText(s(next.message)+"\n"+s(next.destination));
            if(next.width>0){details_->setText(t("%1 × %2 px · %3 秒 · %4 fps","%1 × %2 px · %3 seconds · %4 fps").arg(next.width).arg(next.height).arg(next.duration_ms/1000.,0,'f',3).arg(next.fps,0,'f',3));if(loaded_!=s(next.source)){
                loaded_=s(next.source);first_->setMaximum(next.duration_ms/1000.);last_->setMaximum(next.duration_ms/1000.);first_->setSingleStep(1./std::max(1.,next.fps));last_->setSingleStep(1./std::max(1.,next.fps));first_->setValue(0);last_->setValue(next.duration_ms/1000.);int values[]={0,0,next.width,next.height};for(int n=0;n<4;++n){crop_[n]->setMaximum(n%2?next.height:next.width);crop_[n]->setValue(values[n]);}
                if(draft_&&draft_->source==loaded_){first_->setValue(draft_->first);last_->setValue(draft_->last);for(int n=0;n<4;++n)crop_[n]->setValue(draft_->crop[n]);format_->setCurrentIndex(draft_->format);fps_->setValue(draft_->fps);gif_width_->setCurrentIndex(draft_->width);palette_->setCurrentIndex(draft_->palette);}
            }}else if(next.phase==1)details_->setText(t("正在读取素材信息…","Reading media information…"));
        }snapshot_=next;
    }
public:
    explicit MediaStudioPage(QWidget* parent=nullptr):QWidget(parent){
        backend_=static_cast<UiStudioApi const*>(api.extension(5));if(backend_&&(backend_->version!=LB_STUDIO_UI_ABI||backend_->size!=sizeof(UiStudioApi)))backend_=nullptr;
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(14);outer->addWidget(label(t("让每个片段，刚刚好。","Make every clip just right."),"section"));outer->addWidget(label(t("时间截取 · 画面裁剪 · 视频转 GIF · 独立导出，保留原素材","Time trim · Picture crop · Video to GIF · Export a new file and preserve the source"),"muted",true));
        auto row=new QHBoxLayout;source_=new QLineEdit;source_->setReadOnly(true);source_->setPlaceholderText(t("选择视频或 GIF 文件","Choose a video or GIF"));row->addWidget(source_,1);browse_=button(t("选择素材…","Choose media…"),[this]{choose();});row->addWidget(browse_);outer->addLayout(row);details_=label(t("尚未选择素材","No media selected"),"muted",true);outer->addWidget(details_);
        host_=new QWidget;host_->setAttribute(Qt::WA_NativeWindow);host_->setStyleSheet("background:#0c1016;");host_->setMinimumHeight(220);host_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);host_->hide();outer->addWidget(host_);play_=button(t("播放预览","Play preview"),[this]{if(playing_){stop_preview();return;}if(loaded_.isEmpty())return;host_->show();api.play_file(w(loaded_),uintptr_t(host_->winId()));playing_=true;play_->setText(t("停止预览","Stop preview"));});outer->addWidget(play_,0,Qt::AlignLeft);
        form_=new QWidget;auto form=new QFormLayout(form_);form->setContentsMargins(0,0,0,0);form->setVerticalSpacing(12);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);format_=new SettingsCombo;format_->addItems({t("高画质 MP4","High quality MP4"),t("动态 GIF（最多 256 色）","Animated GIF (up to 256 colours)"),t("无损 MKV（文件较大）","Lossless MKV (larger files)")});form->addRow(t("导出格式","Export format"),format_);
        first_=new SettingsDoubleSpinBox;last_=new SettingsDoubleSpinBox;for(auto box:{first_,last_}){box->setDecimals(3);box->setRange(0,86400);box->setSuffix(t(" 秒"," s"));box->setMinimumWidth(110);}auto times=new QHBoxLayout;times->addWidget(first_);times->addWidget(label(t("至","to")));times->addWidget(last_);form->addRow(t("时间范围","Time range"),times);
        auto crop=new QGridLayout;QString names[]={"X","Y",t("宽","Width"),t("高","Height")};for(int n=0;n<4;++n){crop_[n]=new SettingsSpinBox;crop_[n]->setRange(n<2?0:1,32768);crop_[n]->setPrefix(names[n]+" ");crop_[n]->setMinimumWidth(100);crop->addWidget(crop_[n],n/2,n%2);}form->addRow(t("画面裁剪","Picture crop"),crop);
        fps_=new SettingsSpinBox;fps_->setRange(1,30);fps_->setValue(12);form->addRow(t("GIF 帧率","GIF frame rate"),fps_);gif_width_=new SettingsCombo;gif_width_->addItem(t("保持源分辨率","Keep source resolution"),0);for(int width:{320,640,960})gif_width_->addItem(t("宽 %1 px","%1 px wide").arg(width),width);form->addRow(t("GIF 尺寸","GIF dimensions"),gif_width_);palette_=new SettingsCombo;palette_->addItems({t("快速调色板","Fast palette"),t("精细调色板","Precise palette")});form->addRow(t("GIF 调色板","GIF palette"),palette_);outer->addWidget(form_);
        connect(format_,&QComboBox::currentIndexChanged,this,[this](int index){fps_->setEnabled(index==1);gif_width_->setEnabled(index==1);palette_->setEnabled(index==1);});fps_->setEnabled(false);gif_width_->setEnabled(false);palette_->setEnabled(false);
        progress_=new QProgressBar;progress_->setRange(0,100);progress_->setValue(0);outer->addWidget(progress_);message_=label(t("GIF 使用两次流式处理，不把整段视频帧缓存在内存里。","GIF export uses two streaming passes instead of keeping all frames in memory."),"muted",true);message_->setTextInteractionFlags(Qt::TextSelectableByMouse);outer->addWidget(message_);auto actions=new QHBoxLayout;cancel_=button(t("取消任务","Cancel task"),[this]{if(backend_)backend_->cancel();});actions->addWidget(cancel_);actions->addStretch();export_=button(t("导出新文件…","Export new file…"),[this]{export_media();},nullptr,"primary");actions->addWidget(export_);outer->addLayout(actions);outer->addStretch();
        browse_->setEnabled(backend_);export_->setEnabled(false);play_->setEnabled(false);cancel_->setEnabled(false);form_->setEnabled(false);if(!backend_)message_->setText(t("媒体组件未能加载，请重新安装软件。","The media component could not load. Reinstall the app."));poll_.setInterval(150);connect(&poll_,&QTimer::timeout,this,[this]{refresh();});poll_.start();refresh();
    }
    ~MediaStudioPage()override{remember();stop_preview();}
    void showEvent(QShowEvent* event)override{refresh();poll_.start();QWidget::showEvent(event);}
    void hideEvent(QHideEvent* event)override{remember();stop_preview();poll_.stop();QWidget::hideEvent(event);}
};
}
