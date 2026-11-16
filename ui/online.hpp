#pragma once
#include "ui.hpp"
#include "wallpaper_grid.hpp"
#include "image_loader.hpp"
#include "../src/online_catalog_ui_api.h"
#include "../src/version.hpp"
#include <QtConcurrent>
#include <QtNetwork>
#include <memory>

namespace ui {
class OnlinePage;
class OnlineGridView final:public QListView {
    bool pending_{};
    void relayout(){if(pending_)return;pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;QSize next(wallpaper_grid_cell_width(this),188);if(gridSize()!=next)setGridSize(next);});}
protected:
    bool eventFilter(QObject* object,QEvent* event)override{if(object==viewport()&&(event->type()==QEvent::Resize||event->type()==QEvent::Show))relayout();return QListView::eventFilter(object,event);}
    void resizeEvent(QResizeEvent* event)override{QListView::resizeEvent(event);relayout();}
public:
    explicit OnlineGridView(QWidget* parent=nullptr):QListView(parent){viewport()->installEventFilter(this);connect(verticalScrollBar(),&QScrollBar::rangeChanged,this,[this]{relayout();});}
};
class OnlinePoster final:public QWidget {
    QImage image_;QString placeholder_;
public:
    explicit OnlinePoster(QWidget* parent=nullptr):QWidget(parent) {setMinimumSize(0,160);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    void display(QImage image,QString placeholder={}) {image_=std::move(image);placeholder_=std::move(placeholder);update();}
    bool has_image()const{return !image_.isNull();}
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPainterPath clip;clip.addRoundedRect(QRectF(rect()),9,9);p.setClipPath(clip);p.fillRect(rect(),QColor(state.dark?"#15232a":"#e2eae6"));
        if(!image_.isNull()){QSize size=image_.size().scaled(this->size(),Qt::KeepAspectRatio);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(QRect(QPoint((width()-size.width())/2,(height()-size.height())/2),size),image_);}
        else {p.setPen(QColor(state.dark?"#a6c7b9":"#5a7669"));p.drawText(rect().adjusted(16,12,-16,-12),Qt::AlignCenter|Qt::TextWordWrap,placeholder_);}
    }
};
class OnlineModel final:public QAbstractListModel {
public:
    QVector<int> rows;QJsonArray const* entries{};
    explicit OnlineModel(QJsonArray const* source,QObject* parent):QAbstractListModel(parent),entries(source){}
    int rowCount(QModelIndex const& parent={})const override{return parent.isValid()?0:int(rows.size());}
    QVariant data(QModelIndex const& index,int role)const override {
        if(!index.isValid()||index.row()<0||index.row()>=rows.size())return {};int entry=rows[index.row()];auto item=entries->at(entry).toObject();auto title=item["title"].toObject()[state.english?"en":"zh"].toString();
        if(role==Qt::UserRole)return entry;if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole)return title;if(role==Qt::ToolTipRole)return title+"\n"+item["original_filename"].toString();return {};
    }
    void replace(QVector<int> values){beginResetModel();rows=std::move(values);endResetModel();}
};
class OnlineDelegate final:public QStyledItemDelegate {
    OnlinePage* page_;
public:
    explicit OnlineDelegate(OnlinePage* page);
    QSize sizeHint(QStyleOptionViewItem const&,QModelIndex const&)const override;
    void paint(QPainter*,QStyleOptionViewItem const&,QModelIndex const&)const override;
};
class OnlinePage:public QWidget {
    friend class OnlineDelegate;
    static constexpr qint64 max_cover_bytes=3*1024*1024;
    static constexpr int max_cover_jobs=3;
    struct CoverRequest{int index{},redirects{};quint64 epoch{};QUrl url;QByteArray bytes;bool rejected{};};
    ImageLoader* image_loader_{};QJsonArray entries_;OnlineModel* model_{};QListView* view_{};QLineEdit* search_{};
    QStackedWidget *browse_{},*preview_stack_{};OnlinePoster *poster_{},*inspector_cover_{};QWidget* video_{};
    QLabel *title_{},*meta_{},*attribution_{},*popularity_{},*links_{},*hint_{},*message_{},*status_{},*result_count_{},*empty_{};
    QPushButton *preview_{},*download_{},*import_{},*refresh_button_{};QProgressBar* progress_{};QList<QPushButton*> filters_;
    QNetworkAccessManager* network_{};QHash<QNetworkReply*,std::shared_ptr<CoverRequest>> requests_;QCache<int,QImage> covers_{32};QQueue<int> cover_queue_;QSet<int> cover_pending_,cover_failed_;QTimer cover_pump_;
    UiState snapshot_{};uint32_t serial_{};int selected_{-1},kind_{},inflight_{};quint64 generation_{},cover_epoch_{};
    int preview_intent_{-1};quint64 preview_generation_{};uint32_t preview_serial_{};bool preview_mode_{},waiting_{},media_playing_{},fixture_{},last_dark_{};QString preview_error_;
    QString title(QJsonObject const& item)const{return item["title"].toObject()[state.english?"en":"zh"].toString();}
    QString kind_name(QString kind)const{return kind=="image"?ui::tr("静态","Image"):kind=="gif"?QString("GIF"):ui::tr("视频","Video");}
    QString path(QJsonObject const& item)const{return s(state.data)+"/online/"+item["filename"].toString();}
    QString cover_path(QJsonObject const& item)const{return s(state.data)+"/online/covers/"+QString::fromLatin1(QCryptographicHash::hash(item["thumbnail_url"].toString().toUtf8(),QCryptographicHash::Sha256).toHex())+".png";}
    static bool allowed_cover(QUrl const& url) {
        static QStringList const hosts{"upload.wikimedia.org","thumb.wikimedia.org","commons.wikimedia.org","archive.org","images.unsplash.com","svs.gsfc.nasa.gov"};
        return url.isValid()&&url.scheme()=="https"&&url.userInfo().isEmpty()&&(url.port(-1)==-1||url.port()==443)&&hosts.contains(url.host().toLower())&&url.toString().size()<=4096;
    }
    bool downloaded(QJsonObject const& item)const {
        if(fixture_)return item["fixture_downloaded"].toBool();auto file=path(item);WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(w(file),GetFileExInfoStandard,&info))return false;
        auto bytes=(qulonglong(info.nFileSizeHigh)<<32)|info.nFileSizeLow;auto time=(qulonglong(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
        if(bytes!=qulonglong(item["bytes"].toDouble()))return false;QFile receipt(file+".verified");return receipt.open(QIODevice::ReadOnly)&&receipt.size()<=512&&QString::fromUtf8(receipt.readAll())==item["sha1"].toString()+"\n"+QString::number(bytes)+"\n"+QString::number(time)+"\n";
    }
    QString download_status()const{
        if(snapshot_.download_stage==4)return ui::tr("正在取消下载…","Cancelling download…");
        if(snapshot_.download_stage==3)return ui::tr("下载完成，正在校验并保存…","Verifying and saving the download…");
        if(snapshot_.download_received==0)return ui::tr("正在连接来源 · 已等待 %1 秒","Connecting to source · %1 s elapsed").arg(snapshot_.download_elapsed);
        QString text=ui::tr("%1 / %2 MB · %3%","%1 / %2 MB · %3%").arg(snapshot_.download_received/1000000.,0,'f',2).arg(snapshot_.download_total/1000000.,0,'f',2).arg(snapshot_.download_percent);
        if(snapshot_.download_idle>=3)text+=ui::tr("\n等待网络响应 · %1 秒，可取消重试","\nWaiting for network · %1 s; you can cancel and retry").arg(snapshot_.download_idle);
        else if(snapshot_.download_elapsed>0)text+=QString(" · %1 KB/s").arg(snapshot_.download_received/1000./snapshot_.download_elapsed,0,'f',0);
        return text;
    }
    QString download_text(int index)const {
        if(index<0||index>=entries_.size())return {};if(downloaded(entries_[index].toObject()))return ui::tr("已下载","Downloaded");
        if(snapshot_.download_busy&&!snapshot_.download_preview&&snapshot_.download_index==index)return snapshot_.download_received?ui::tr("下载中 %1%","Downloading %1%").arg(snapshot_.download_percent):ui::tr("正在连接…","Connecting…");return ui::tr("下载原文件","Download original");
    }
    QImage cover(int index) {
        if(auto image=covers_.object(index))return *image;
        if(!fixture_&&isVisible()&&index>=0&&index<entries_.size()&&!cover_pending_.contains(index)&&!cover_failed_.contains(index)){cover_pending_.insert(index);cover_queue_.enqueue(index);if(!cover_pump_.isActive())cover_pump_.start();}return {};
    }
    void pump_covers(){if(fixture_||!isVisible())return;while(inflight_<max_cover_jobs&&!cover_queue_.isEmpty()){int index=cover_queue_.dequeue();++inflight_;QString cached=cover_path(entries_[index].toObject());QFileInfo info(cached);if(info.isFile()&&info.size()>0&&info.size()<=max_cover_bytes)decode_cover(index,{},cover_epoch_,cached,false);else fetch_cover(std::make_shared<CoverRequest>(CoverRequest{index,0,cover_epoch_,QUrl(entries_[index].toObject()["thumbnail_url"].toString()),{},false}));}}
    void finish_cover(int index,quint64 epoch,QImage image) {
        if(epoch!=cover_epoch_)return;inflight_=std::max(0,inflight_-1);cover_pending_.remove(index);
        if(image.isNull())cover_failed_.insert(index);else covers_.insert(index,new QImage(std::move(image)));view_->viewport()->update();
        if(index==selected_){auto image=cover(index);inspector_cover_->display(image,kind_name(entries_[index].toObject()["kind"].toString()));if(!media_playing_)poster_->display(image,ui::tr("封面暂不可用","Cover unavailable"));}pump_covers();
    }
    void decode_cover(int index,QByteArray bytes,quint64 epoch,QString file,bool save) {
        auto watcher=new QFutureWatcher<QImage>(this);connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,index,epoch,file,save]{auto image=watcher->result();watcher->deleteLater();if(epoch!=cover_epoch_)return;if(image.isNull()&&!save){QFile::remove(file);fetch_cover(std::make_shared<CoverRequest>(CoverRequest{index,0,epoch,QUrl(entries_[index].toObject()["thumbnail_url"].toString()),{},false}));return;}finish_cover(index,epoch,std::move(image));});
        watcher->setFuture(QtConcurrent::run([bytes=std::move(bytes),file=std::move(file),save]()mutable {
            if(bytes.isEmpty()){QFile input(file);if(!input.open(QIODevice::ReadOnly)||input.size()>max_cover_bytes)return QImage{};bytes=input.read(max_cover_bytes+1);}
            if(bytes.size()>max_cover_bytes)return QImage{};QBuffer source(&bytes);source.open(QIODevice::ReadOnly);QImageReader reader(&source);reader.setAutoTransform(true);auto size=reader.size();if(!size.isValid()||qint64(size.width())*size.height()>16*1024*1024)return QImage{};
            reader.setScaledSize(size.scaled(QSize(480,320),Qt::KeepAspectRatio));auto image=reader.read();if(image.isNull())return image;if(image.width()>480||image.height()>320)image=image.scaled(QSize(480,320),Qt::KeepAspectRatio,Qt::SmoothTransformation);
            if(save){QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile output(file);if(output.open(QIODevice::WriteOnly)&&image.save(&output,"PNG"))output.commit();auto folder=QFileInfo(file).absolutePath();QDir directory(folder);auto files=directory.entryInfoList({"*.png"},QDir::Files|QDir::NoSymLinks,QDir::Time);qint64 total=0;for(auto const& item:files){if(!QRegularExpression("^[a-f0-9]{64}\\.png$").match(item.fileName()).hasMatch())continue;total+=item.size();if(total>64*1024*1024&&item.absoluteFilePath()!=file)QFile::remove(item.absoluteFilePath());}}return image;
        }));
    }
    void fetch_cover(std::shared_ptr<CoverRequest> job) {
        if(job->epoch!=cover_epoch_||!allowed_cover(job->url)){finish_cover(job->index,job->epoch,{});return;}
        QNetworkRequest request(job->url);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(15000);request.setRawHeader("User-Agent","LightBlockDesktopNative/0.1 (wallpaper cover preview)");request.setRawHeader("Accept","image/*");auto reply=network_->get(request);reply->setReadBufferSize(64*1024);requests_[reply]=job;
        connect(reply,&QNetworkReply::metaDataChanged,this,[reply,job]{auto size=reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();if(size>max_cover_bytes){job->rejected=true;reply->abort();}});
        connect(reply,&QIODevice::readyRead,this,[reply,job]{while(reply->bytesAvailable()>0){auto chunk=reply->read(std::min<qint64>(64*1024,max_cover_bytes+1-job->bytes.size()));job->bytes+=chunk;if(job->bytes.size()>max_cover_bytes){job->rejected=true;reply->abort();break;}if(chunk.isEmpty())break;}});
        connect(reply,&QNetworkReply::finished,this,[this,reply,job]{requests_.remove(reply);auto redirect=reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();auto error=reply->error();reply->deleteLater();if(job->epoch!=cover_epoch_)return;
            if(!job->rejected&&status>=300&&status<400&&!redirect.isEmpty()&&job->redirects<4){job->url=job->url.resolved(redirect);++job->redirects;job->bytes.clear();fetch_cover(job);return;}
            if(job->rejected||error!=QNetworkReply::NoError||status!=200||job->bytes.isEmpty()){finish_cover(job->index,job->epoch,{});return;}decode_cover(job->index,std::move(job->bytes),job->epoch,cover_path(entries_[job->index].toObject()),true);
        });
    }
    void pause_covers(){++cover_epoch_;cover_pump_.stop();cover_queue_.clear();cover_pending_.clear();inflight_=0;auto replies=requests_.keys();requests_.clear();for(auto reply:replies){disconnect(reply,nullptr,this,nullptr);reply->abort();reply->deleteLater();}}
    void refresh_catalog(){
        if(fixture_)return;
        if(snapshot_.download_busy){status_->setText(ui::tr("请先结束当前下载，再刷新目录。","Finish the current download before refreshing the catalog."));return;}
        auto catalog_api=api.extension?static_cast<UiCatalogApi const*>(api.extension(6)):nullptr;
        if(!catalog_api||catalog_api->version!=LB_ONLINE_CATALOG_ABI||catalog_api->size!=sizeof(UiCatalogApi)){status_->setText(ui::tr("目录组件未就绪。","Catalog service is unavailable."));return;}
        refresh_button_->setEnabled(false);status_->setText(ui::tr("正在获取在线目录…","Fetching the online catalog…"));
        QNetworkRequest request(QUrl("https://raw.githubusercontent.com/GalokPeng/light-block-desktop/main/assets/online-wallpapers-v2.json"));request.setTransferTimeout(15000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setRawHeader("User-Agent","Light-Block-Desktop/" LB_VERSION_UTF8);
        auto reply=network_->get(request);reply->setReadBufferSize(64*1024);auto bytes=std::make_shared<QByteArray>();
        connect(reply,&QNetworkReply::metaDataChanged,this,[reply]{if(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()>2*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::readyRead,this,[reply,bytes]{*bytes+=reply->readAll();if(bytes->size()>2*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,catalog_api]{
            *bytes+=reply->readAll();bool ok=reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&!bytes->isEmpty()&&bytes->size()<=2*1024*1024;reply->deleteLater();refresh_button_->setEnabled(true);
            if(ok){QString directory=s(state.data)+"/online";ok=QDir().mkpath(directory);QTemporaryFile temporary(directory+"/catalog-download-XXXXXX.json");ok=ok&&temporary.open();if(ok){ok=temporary.write(*bytes)==bytes->size()&&temporary.flush();auto path=temporary.fileName();temporary.close();if(ok)ok=catalog_api->install(w(path))!=0;}}
            if(!ok){status_->setText(ui::tr("目录刷新失败，继续使用已验证的本地目录。","Refresh failed. The verified local catalog is still available."));return;}
            close_preview();pause_covers();selected_=-1;covers_.clear();cover_failed_.clear();load_catalog();QStringList kinds{"image","gif","video"};for(int n=0;n<3;++n){int count=0;for(auto item:entries_)if(item.toObject()["kind"]==kinds[n])++count;filters_[n]->setText(kind_name(kinds[n])+"  "+QString::number(count));}filter_entries();status_->setText(ui::tr("在线目录已更新。","Online catalog updated."));
        });
    }
    void load_catalog(){auto catalog_api=api.extension?static_cast<UiCatalogApi const*>(api.extension(6)):nullptr;QString path=catalog_api&&catalog_api->version==LB_ONLINE_CATALOG_ABI&&catalog_api->size==sizeof(UiCatalogApi)?s(catalog_api->path()):QString{};QFile catalog(path.isEmpty()?s(state.directory)+"/assets/online-wallpapers-v2.json":path);if(catalog.open(QIODevice::ReadOnly)&&catalog.size()<=2*1024*1024){auto object=QJsonDocument::fromJson(catalog.readAll()).object();if(object["schema_version"].toInt()==4&&object["entries"].toArray().size()<=120)entries_=object["entries"].toArray();}}
    void filter_entries() {
        int keep=selected_;close_preview();QString query=search_->text().trimmed();QStringList kinds{"image","gif","video"};QVector<int> rows;
        for(int n=0;n<entries_.size();++n){auto item=entries_[n].toObject();if(item["kind"].toString()!=kinds[kind_])continue;QStringList words{item["author"].toString(),item["search_text"].toString()};for(auto value:item["title"].toObject())words<<value.toString();for(auto value:item["tags"].toArray())words<<value.toString();if(query.isEmpty()||words.join(' ').contains(query,Qt::CaseInsensitive))rows<<n;}
        model_->replace(rows);result_count_->setText(ui::tr("%1 项","%1 items").arg(rows.size()));empty_->setVisible(rows.isEmpty());selected_=-1;
        if(!rows.isEmpty()){int row=rows.indexOf(keep);view_->setCurrentIndex(model_->index(row<0?0:row));}else update_details();
    }
    void selected(QModelIndex const& current) {
        close_preview();selected_=current.isValid()?current.data(Qt::UserRole).toInt():-1;preview_error_.clear();if(selected_>=0){auto image=cover(selected_);auto kind=kind_name(entries_[selected_].toObject()["kind"].toString());poster_->display(image,kind);inspector_cover_->display(image,kind);}update_details();
    }
    void close_preview() {
        ++generation_;if(image_loader_)image_loader_->cancel();if(waiting_&&!fixture_)api.state(&snapshot_);bool cancel=waiting_&&snapshot_.download_busy&&snapshot_.download_preview&&snapshot_.download_index==preview_intent_;bool was_media=media_playing_;preview_intent_=-1;waiting_=preview_mode_=media_playing_=false;
        if(!fixture_){if(cancel)api.online(-1,0);if(was_media)api.preview(-1,0);}if(video_)video_->hide();if(preview_stack_)preview_stack_->setCurrentIndex(0);if(browse_)browse_->setCurrentIndex(0);if(preview_)preview_->setText(ui::tr("开始预览","Start preview"));
    }
    void apply_selection(){if(selected_>=0&&!fixture_){close_preview();api.online(selected_,3);}}
    void toggle_preview() {
        if(preview_mode_){close_preview();update_details();return;}if(selected_<0)return;if(!fixture_)api.state(&snapshot_);if(snapshot_.download_busy){update_details();return;}
        preview_error_.clear();preview_mode_=true;preview_intent_=selected_;preview_generation_=++generation_;preview_serial_=snapshot_.download_serial;media_playing_=false;browse_->setCurrentIndex(1);preview_stack_->setCurrentIndex(0);
        auto item=entries_[selected_].toObject();poster_->display(cover(selected_),ui::tr("正在准备预览…","Preparing preview…"));
        if(item["kind"]=="image"&&(!cover(selected_).isNull()||downloaded(item))){waiting_=false;if(downloaded(item)&&!fixture_)play(path(item));}
        else {waiting_=true;if(!fixture_){api.online(selected_,1);api.state(&snapshot_);}}update_details();
    }
    void play(QString file) {
        if(selected_<0||!preview_mode_||preview_intent_!=selected_||preview_generation_!=generation_)return;waiting_=false;auto item=entries_[selected_].toObject();
        if(item["kind"]=="image") {
            auto generation=generation_;if(!image_loader_)image_loader_=new ImageLoader(this);image_loader_->request(file,0,{1600,1000},[this,generation](ImageMetadata result){if(generation==generation_&&preview_mode_&&!result.image.isNull())poster_->display(std::move(result.image));});
        }else {media_playing_=true;preview_stack_->setCurrentIndex(1);video_->show();if(!fixture_)api.play_file(w(file),uintptr_t(video_->winId()));}update_details();
    }
    void update_details() {
        bool valid=selected_>=0&&selected_<entries_.size();preview_->setEnabled(valid&&(preview_mode_||!snapshot_.download_busy));download_->setEnabled(valid&&!snapshot_.download_busy);import_->setEnabled(false);
        if(!valid){title_->setText(ui::tr("选择一张壁纸","Select a wallpaper"));meta_->clear();attribution_->clear();popularity_->clear();links_->clear();message_->clear();inspector_cover_->display({},ui::tr("在线壁纸","Online wallpapers"));progress_->hide();download_->setText(ui::tr("下载原文件","Download original"));return;}
        auto item=entries_[selected_].toObject();title_->setText(title(item));meta_->setText(QString("%1 × %2  ·  %3 MB  ·  %4").arg(item["width"].toInt()).arg(item["height"].toInt()).arg(item["bytes"].toDouble()/1000000.,0,'f',1).arg(kind_name(item["kind"].toString())));
        attribution_->setText(ui::tr("作者：%1\n许可：%2","Author: %1\nLicense: %2").arg(item["author"].toString(),item["license"].toString()));attribution_->setToolTip(item["restrictions"].toString());auto metrics=item["popularity"].toObject();QStringList summary;
        for(auto [key,zh,en]:{std::tuple{"votes","公开评选支持票：%1","Public votes: %1"},std::tuple{"views","来源浏览量：%1","Source views: %1"},std::tuple{"favorites","来源收藏数：%1","Source favorites: %1"},std::tuple{"likes","来源点赞数：%1","Source likes: %1"}}){auto value=metrics[key];if(!value.isUndefined()&&!value.isNull())summary<<ui::tr(zh,en).arg(QLocale().toString(qint64(value.toDouble())));}
        popularity_->setText(summary.isEmpty()?ui::tr("来源未公开热度数据。","The source does not publish popularity data."):summary.join('\n'));popularity_->setToolTip(ui::tr("核对日期：%1；不同来源指标不能直接比较。","Checked: %1. Metrics from different sources are not directly comparable.").arg(metrics["checked_at"].toString()));
        QStringList links;auto add_link=[&](QString url,QString name){QUrl parsed(url);if(parsed.scheme()=="https"&&parsed.userInfo().isEmpty()&&!parsed.host().isEmpty())links<<QString("<a style=\"color:%1\" href=\"%2\">%3</a>").arg(state.dark?"#a6e4ca":"#286b54",url.toHtmlEscaped(),name.toHtmlEscaped());};
        add_link(item["source_url"].toString(),ui::tr("作品来源","Source"));add_link(item["license_url"].toString(),ui::tr("查看授权","License"));add_link(metrics["evidence_url"].toString(),ui::tr("热度依据","Evidence"));links_->setText(links.join("　·　"));
        bool busy=snapshot_.download_busy&&snapshot_.download_index==selected_;bool ready=downloaded(item);
        preview_->setText(preview_mode_?(waiting_?ui::tr("预览加载 %1% · 点击取消","Loading %1% · Cancel").arg(snapshot_.download_percent):ui::tr("停止预览 · 返回图库","Stop preview · Back")):ui::tr("开始预览","Start preview"));
        download_->setText(busy&&!snapshot_.download_preview?ui::tr("取消下载","Cancel download"):download_text(selected_));download_->setEnabled((busy&&!snapshot_.download_preview)||(!ready&&!snapshot_.download_busy));import_->setEnabled(ready&&!snapshot_.download_busy);
        progress_->setVisible(busy);progress_->setRange(0,busy&&(!snapshot_.download_received||snapshot_.download_stage>=3)?0:100);progress_->setValue(snapshot_.download_percent);message_->setText(busy?download_status():preview_error_);
        // A stall adds a second status line. Reflow before painting so its text
        // does not spend a frame clipped to the previous one-line geometry.
        if(auto panel=message_->parentWidget();panel&&panel->layout())panel->layout()->activate();
        hint_->setText(ui::tr("封面用于浏览，下载保留原文件画质。动态预览默认静音。","Covers are for browsing. Downloads preserve original quality. Motion previews are muted."));
    }
protected:
    void showEvent(QShowEvent* event)override {QWidget::showEvent(event);view_->viewport()->update();if(selected_>=0)inspector_cover_->display(cover(selected_),kind_name(entries_[selected_].toObject()["kind"].toString()));update_details();}
    void hideEvent(QHideEvent* event)override {suspend();QWidget::hideEvent(event);}
public:
    struct ViewState {int kind{};QString query,id;};
    ViewState view_state()const{return {kind_,search_->text(),selected_>=0&&selected_<entries_.size()?entries_[selected_].toObject()["id"].toString():QString{}};}
    void restore_view(ViewState const& saved){
        kind_=std::clamp(saved.kind,0,2);filters_[kind_]->setChecked(true);{QSignalBlocker block(search_);search_->setText(saved.query);}selected_=-1;for(int n=0;n<entries_.size();++n)if(entries_[n].toObject()["id"].toString()==saved.id){selected_=n;break;}filter_entries();if(view_->currentIndex().isValid())view_->scrollTo(view_->currentIndex(),QAbstractItemView::PositionAtCenter);
    }
    explicit OnlinePage(QWidget* parent=nullptr,bool allow_network=true,QJsonArray supplied={}):QWidget(parent),fixture_(!allow_network) {
        if(supplied.isEmpty())load_catalog();else entries_=std::move(supplied);snapshot_=state;serial_=state.download_serial;last_dark_=state.dark;network_=new QNetworkAccessManager(this);cover_pump_.setSingleShot(true);cover_pump_.setInterval(0);connect(&cover_pump_,&QTimer::timeout,this,[this]{pump_covers();});
        auto layout=new QHBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(16);auto left=new QVBoxLayout;left->setSpacing(12);auto top=new QHBoxLayout;
        search_=new QLineEdit;search_->setPlaceholderText(ui::tr("搜索在线壁纸、标题或标签…","Search wallpapers, titles or tags…"));search_->setAccessibleName(ui::tr("搜索在线壁纸","Search online wallpapers"));search_->setClearButtonEnabled(true);top->addWidget(search_,1);
        refresh_button_=button(ui::tr("刷新","Refresh"),[this]{refresh_catalog();});top->addWidget(refresh_button_);left->addLayout(top);
        auto filter=new QHBoxLayout;auto group=new QButtonGroup(this);QStringList kinds{"image","gif","video"};for(int n=0;n<3;++n){int count=0;for(auto entry:entries_)if(entry.toObject()["kind"].toString()==kinds[n])++count;auto b=button(kind_name(kinds[n])+"  "+QString::number(count),[this,n]{kind_=n;filter_entries();},nullptr,"pill");b->setCheckable(true);b->setChecked(n==0);filters_<<b;group->addButton(b);filter->addWidget(b);}filter->addStretch();result_count_=label({},"muted");filter->addWidget(result_count_);left->addLayout(filter);
        view_=new OnlineGridView;view_->setAccessibleName(ui::tr("在线壁纸","Online wallpapers"));view_->setViewMode(QListView::IconMode);view_->setResizeMode(QListView::Adjust);view_->setMovement(QListView::Static);view_->setUniformItemSizes(true);view_->setGridSize({216,188});view_->setMouseTracking(true);view_->setSpacing(0);view_->setFrameShape(QFrame::NoFrame);view_->setEditTriggers(QAbstractItemView::NoEditTriggers);view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);view_->setStyleSheet("QListView{background:transparent;border:0;outline:0;}");model_=new OnlineModel(&entries_,this);view_->setModel(model_);view_->setItemDelegate(new OnlineDelegate(this));
        browse_=new QStackedWidget;browse_->addWidget(view_);preview_stack_=new QStackedWidget;poster_=new OnlinePoster;preview_stack_->addWidget(poster_);video_=new QWidget;video_->setAttribute(Qt::WA_NativeWindow);video_->setStyleSheet("background:#0c1016;");preview_stack_->addWidget(video_);browse_->addWidget(preview_stack_);left->addWidget(browse_,1);empty_=label(ui::tr("没有匹配的壁纸，试试其他关键词。","No matching wallpapers. Try another keyword."),"muted",true);empty_->hide();left->addWidget(empty_);status_=label(ui::tr("精选直链 · 图片与视频由原始来源提供","Curated links · Images and videos come from their original sources"),"muted",true);left->addWidget(status_);layout->addLayout(left,1);
        auto panel=new QFrame;panel->setObjectName("panel");panel->setFixedWidth(282);auto right=new QVBoxLayout(panel);right->setContentsMargins(16,18,16,16);right->setSpacing(10);title_=label({},"section",true);title_->setTextFormat(Qt::PlainText);right->addWidget(title_);inspector_cover_=new OnlinePoster;inspector_cover_->setFixedHeight(166);right->addWidget(inspector_cover_);preview_=button(ui::tr("开始预览","Start preview"),[this]{toggle_preview();});right->addWidget(preview_);
        meta_=label({},"muted",true);attribution_=label({},"muted",true);attribution_->setTextFormat(Qt::PlainText);popularity_=label({},"muted",true);right->addWidget(meta_);right->addWidget(attribution_);right->addWidget(popularity_);links_=new QLabel;links_->setWordWrap(true);links_->setOpenExternalLinks(false);connect(links_,&QLabel::linkActivated,this,[](QString const& link){QUrl url(link);if(url.scheme()=="https"&&url.userInfo().isEmpty())QDesktopServices::openUrl(url);});right->addWidget(links_);hint_=label({},"muted",true);right->addWidget(hint_);right->addStretch();message_=label({},"muted",true);right->addWidget(message_);progress_=new QProgressBar;progress_->setRange(0,100);progress_->hide();right->addWidget(progress_);
        download_=button(ui::tr("下载原文件","Download original"),[this]{if(selected_<0||fixture_)return;preview_error_.clear();if(snapshot_.download_busy&&!snapshot_.download_preview&&snapshot_.download_index==selected_)api.online(-1,0);else api.online(selected_,0);UiState latest{};api.state(&latest);refresh(latest);},nullptr,"primary");right->addWidget(download_);
        import_=button(ui::tr("应用到桌面  →","Apply to desktop  →"),[this]{apply_selection();},nullptr,"secondary");right->addWidget(import_);layout->addWidget(panel);
        connect(search_,&QLineEdit::textChanged,this,[this]{filter_entries();});connect(view_->selectionModel(),&QItemSelectionModel::currentChanged,this,[this](QModelIndex const& current){selected(current);});connect(view_,&QListView::doubleClicked,this,[this]{toggle_preview();});filter_entries();
    }
    ~OnlinePage()override {pause_covers();}
    void suspend(){close_preview();pause_covers();covers_.clear();}
    bool selection_test(){auto query=search_->text();search_->setText("__no_wallpaper_matches_this_fixture__");refresh(snapshot_);bool empty=selected_==-1&&!preview_->isEnabled()&&!download_->isEnabled()&&!import_->isEnabled();search_->setText(query);return empty;}
    void refresh(UiState const& snapshot) {
        bool changed=snapshot_.download_received!=snapshot.download_received||snapshot_.download_stage!=snapshot.download_stage||snapshot_.download_elapsed!=snapshot.download_elapsed||snapshot_.download_idle!=snapshot.download_idle||snapshot_.download_busy!=snapshot.download_busy||snapshot_.download_percent!=snapshot.download_percent||snapshot_.download_index!=snapshot.download_index||last_dark_!=bool(state.dark);last_dark_=state.dark;snapshot_=snapshot;bool completed=snapshot.download_serial!=serial_;serial_=snapshot.download_serial;
        if(completed){view_->viewport()->update();bool matching=waiting_&&preview_mode_&&preview_intent_==selected_&&preview_generation_==generation_&&snapshot.download_preview&&snapshot.download_index==preview_intent_&&snapshot.download_serial!=preview_serial_&&isVisible();if(matching){if(snapshot.download_error){close_preview();preview_error_=ui::tr("预览暂不可用，可重试或先下载原文件。","Preview is unavailable. Retry or download the original first.");}else play(s(snapshot.download_path));}else if(snapshot.download_error&&snapshot.download_index==selected_)preview_error_=ui::tr("下载或校验未完成，请重试或打开作品来源。","Download or verification did not complete. Retry or open the source.");}
        if(completed||changed){update_details();view_->viewport()->update();}
    }
    bool fixture_tests(QString const& directory) {
        if(!allowed_cover(QUrl("https://svs.gsfc.nasa.gov/vis/cover.jpg"))||allowed_cover(QUrl("https://svs.gsfc.nasa.gov.evil.example/cover.jpg"))||allowed_cover(QUrl("https://user@svs.gsfc.nasa.gov/cover.jpg"))||allowed_cover(QUrl("http://svs.gsfc.nasa.gov/cover.jpg")))return false;
        QJsonArray fixtures;for(int n=0;n<8;++n){QString kind=n<4?"image":n<6?"gif":"video";fixtures.append(QJsonObject{{"id",QString("fixture-%1").arg(n)},{"title",QJsonObject{{"zh",QString("测试风景 %1").arg(n+1)},{"en",QString("Fixture landscape %1").arg(n+1)}}},{"kind",kind},{"filename",QString("fixture-%1.jpg").arg(n)},{"thumbnail_url",QString("https://thumb.wikimedia.org/fixture-%1.png").arg(n)},{"width",1920},{"height",1080},{"bytes",1200000+n*500000},{"author","Local UI fixture"},{"license","CC0"},{"source_url","https://example.org/source"},{"license_url","https://creativecommons.org/publicdomain/zero/1.0/"},{"tags",QJsonArray{"fixture-tag"}},{"fixture_downloaded",n==0}});}
        OnlinePage fixture(nullptr,false,fixtures);fixture.setObjectName("root");fixture.setAttribute(Qt::WA_StyledBackground);fixture.setStyleSheet(window()->styleSheet());fixture.resize(1000,660);
        for(int n=0;n<8;++n){QImage cover(480,270,QImage::Format_RGB32);QPainter p(&cover);QLinearGradient gradient(0,0,480,270);gradient.setColorAt(0,QColor::fromHsv((n*41+190)%360,130,95));gradient.setColorAt(1,QColor::fromHsv((n*41+225)%360,110,210));p.fillRect(cover.rect(),gradient);p.setPen(QColor("#d5e9e0"));p.setFont(QFont("Segoe UI",16));p.drawText(cover.rect(),Qt::AlignCenter,QString("LOCAL FIXTURE %1").arg(n+1));p.end();fixture.covers_.insert(n,new QImage(cover));}
        fixture.show();qApp->processEvents();QDir().mkpath(directory);auto render=[&](QString name){QImage image(fixture.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);fixture.render(&image);return image.save(directory+"/"+name+".png");};
        for(int n=0;n<8;++n){qApp->processEvents();fixture.view_->doItemsLayout();}QRect row0=fixture.view_->visualRect(fixture.model_->index(0)),row2=fixture.view_->visualRect(fixture.model_->index(2)),row3=fixture.view_->visualRect(fixture.model_->index(3));bool threeColumns=row0.top()==row2.top()&&row3.top()>row0.top()&&row2.right()<fixture.view_->viewport()->width();bool ok=render("online-card-grid")&&threeColumns;fixture.toggle_preview();qApp->processEvents();bool inline_image=fixture.browse_->currentIndex()==1&&fixture.poster_->has_image();ok=render("online-image-preview")&&ok;fixture.close_preview();fixture.kind_=2;fixture.filters_[2]->setChecked(true);fixture.filter_entries();fixture.toggle_preview();UiState completed{};completed.download_serial=fixture.serial_+1;completed.download_index=fixture.selected_;completed.download_preview=1;int initial=fixture.selected_;fixture.selected(fixture.model_->index(1));fixture.selected(fixture.model_->index(0));fixture.refresh(completed);bool stale=!fixture.preview_mode_&&!fixture.media_playing_&&fixture.selected_==initial;fixture.toggle_preview();completed.download_serial++;fixture.refresh(completed);bool dynamic=fixture.media_playing_&&fixture.preview_stack_->currentIndex()==1;fixture.suspend();bool stopped=!fixture.media_playing_&&!fixture.preview_mode_;fixture.kind_=0;fixture.filters_[0]->setChecked(true);fixture.filter_entries();fixture.search_->setText("fixture-tag");bool tags=fixture.model_->rowCount()==4;fixture.search_->setText("landscape");bool bilingual=fixture.model_->rowCount()==4;bool empty=fixture.selection_test();ok=ok&&inline_image&&stale&&dynamic&&stopped&&tags&&bilingual&&empty&&fixture.requests_.isEmpty();
        static QStringList calls;calls.clear();auto oldPreview=api.preview;auto oldOnline=api.online;api.preview=[](int index,uintptr_t){calls<<QString("preview:%1").arg(index);};api.online=[](int index,int action){calls<<QString("online:%1:%2").arg(index).arg(action);};fixture.fixture_=false;fixture.selected_=0;fixture.waiting_=false;fixture.media_playing_=fixture.preview_mode_=true;fixture.apply_selection();fixture.fixture_=true;api.preview=oldPreview;api.online=oldOnline;bool applyOrder=calls==QStringList{"preview:-1","online:0:3"}&&!fixture.media_playing_&&!fixture.preview_mode_;ok=ok&&applyOrder;
        UiState downloading{};downloading.download_busy=1;downloading.download_index=0;downloading.download_stage=1;downloading.download_total=1200000;downloading.download_elapsed=2;
        fixture.refresh(downloading);bool connection=fixture.progress_->maximum()==0&&fixture.message_->text().contains("2");
        ok=render("online-download-connecting")&&ok;downloading.download_stage=2;downloading.download_received=300000;downloading.download_percent=25;fixture.refresh(downloading);
        bool transfer=fixture.progress_->maximum()==100&&fixture.progress_->value()==25&&fixture.message_->text().contains("0.30");
        downloading.download_idle=6;downloading.download_elapsed=8;fixture.refresh(downloading);bool stall=fixture.message_->text().contains("6")&&fixture.message_->text().contains('\n')&&!fixture.message_->text().contains("\\n");
        ok=render("online-download-waiting")&&ok;downloading.download_stage=3;fixture.refresh(downloading);bool verifying=fixture.progress_->maximum()==0&&fixture.message_->text().contains(ui::tr("校验","Verifying"));
        downloading.download_stage=4;fixture.refresh(downloading);bool cancelling=fixture.message_->text().contains(ui::tr("取消","Cancelling"));
        downloading.download_busy=0;fixture.refresh(downloading);bool hidden=fixture.progress_->isHidden();ok=ok&&connection&&transfer&&stall&&verifying&&cancelling&&hidden;
        QJsonObject result{{"download_connect_transfer_stall_verify_cancel",connection&&transfer&&stall&&verifying&&cancelling&&hidden},{"passed",ok},{"apply_stops_preview_first",applyOrder},{"three_columns_fit_viewport",threeColumns},{"inline_image_preview",inline_image},{"inline_motion_preview_host",dynamic},{"stale_completion_ignored",stale},{"hide_stops_preview",stopped},{"tag_search",tags},{"bilingual_search",bilingual},{"empty_disables_actions",empty},{"network_requests",int(fixture.requests_.size())},{"max_cover_jobs",max_cover_jobs},{"cover_memory_entries",32}};QSaveFile file(directory+"/online-ui-checks.json");if(!file.open(QIODevice::WriteOnly))return false;auto bytes=QJsonDocument(result).toJson();return file.write(bytes)==bytes.size()&&file.commit()&&ok;
    }
};
inline OnlineDelegate::OnlineDelegate(OnlinePage* page):QStyledItemDelegate(page),page_(page){}
inline QSize OnlineDelegate::sizeHint(QStyleOptionViewItem const&,QModelIndex const&)const{return {std::max(1,page_->view_->gridSize().width()),184};}
inline void OnlineDelegate::paint(QPainter* painter,QStyleOptionViewItem const& option,QModelIndex const& index)const {
    int n=index.data(Qt::UserRole).toInt();if(n<0||n>=page_->entries_.size())return;auto entry=page_->entries_[n].toObject();bool selected=option.state&QStyle::State_Selected,hover=option.state&QStyle::State_MouseOver;bool light=!state.dark;QRectF area=QRectF(option.rect).adjusted(4,4,-4,-4);painter->save();painter->setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);painter->setPen(QPen(QColor(selected?"#538f77":hover?"#8daba0":light?"#d3ded9":"#35414d"),selected?1.5:1));painter->setBrush(QColor(light?"#ffffff":"#1c2631"));painter->drawRoundedRect(area,12,12);
    QRectF cover(area.x()+7,area.y()+7,area.width()-14,101);QPainterPath clip;clip.addRoundedRect(cover,7,7);painter->save();painter->setClipPath(clip);painter->fillRect(cover,QColor(light?"#e2eae6":"#243641"));auto image=page_->cover(n);if(!image.isNull()){QSize size=image.size().scaled(cover.size().toSize(),Qt::KeepAspectRatioByExpanding);painter->drawImage(QRectF(cover.center()-QPointF(size.width()/2.,size.height()/2.),size),image);}else{painter->setPen(QColor(light?"#5a7669":"#a6c7b9"));painter->drawText(cover,Qt::AlignCenter,page_->kind_name(entry["kind"].toString()));}painter->restore();
    painter->setPen(QColor(light?"#243a30":"#edf4ef"));painter->setFont(QFont("Microsoft YaHei UI",10,QFont::Medium));auto text=painter->fontMetrics().elidedText(page_->title(entry),Qt::ElideRight,int(area.width()-22));painter->drawText(QRectF(area.x()+11,area.y()+113,area.width()-22,23),Qt::AlignVCenter,text);painter->setFont(QFont("Microsoft YaHei UI",8));painter->setPen(QColor(light?"#597064":"#a3b5ad"));painter->drawText(QRectF(area.x()+11,area.y()+138,area.width()-22,16),Qt::AlignVCenter,QString("%1 × %2  ·  %3 MB").arg(entry["width"].toInt()).arg(entry["height"].toInt()).arg(entry["bytes"].toDouble()/1000000.,0,'f',1));painter->setPen(QColor(light?"#3d8062":"#a6e4ca"));painter->drawText(QRectF(area.x()+11,area.y()+158,area.width()-22,16),Qt::AlignVCenter,page_->download_text(n));painter->restore();
}
}
