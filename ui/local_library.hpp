#pragma once
#include "ui.hpp"
#include "wallpaper_grid.hpp"
#include <QCache>
#include <functional>

namespace ui {
QImage bitmap(uintptr_t handle);
struct LocalWallpaper {
    int index{},kind{},builtin{-1};QString title,path;
    static LocalWallpaper from(UiItem const& item,int index){return {index,item.kind,item.builtin,s(item.title),s(item.path)};}
};

class LocalLibraryModel final:public QAbstractListModel {
    QList<LocalWallpaper> all_,visible_;
    QHash<int,int> positions_;
    mutable QCache<QString,QImage> images_{24};
public:
    std::function<QImage(int)> thumbnail;
    explicit LocalLibraryModel(QObject* parent=nullptr):QAbstractListModel(parent){thumbnail=[](int index){return api.thumbnail?bitmap(api.thumbnail(index)):QImage{};};}
    int rowCount(QModelIndex const& parent={})const override{return parent.isValid()?0:int(visible_.size());}
    QVariant data(QModelIndex const& index,int role)const override{
        auto item=at(index);if(!item)return {};if(role==Qt::DisplayRole||role==Qt::ToolTipRole||role==Qt::AccessibleTextRole)return item->title;if(role==Qt::UserRole)return item->index;return {};
    }
    LocalWallpaper const* at(QModelIndex const& index)const{return index.isValid()&&index.row()>=0&&index.row()<visible_.size()?&visible_[index.row()]:nullptr;}
    void reset(QList<LocalWallpaper> items,int kind,QString const& query){
        beginResetModel();all_=std::move(items);visible_.clear();positions_.clear();visible_.reserve(all_.size());
        for(auto const& item:all_)if((kind<0||kind==item.kind)&&(query.isEmpty()||item.title.contains(query,Qt::CaseInsensitive))){positions_.insert(item.index,int(visible_.size()));visible_.push_back(item);}endResetModel();
    }
    QModelIndex index_for(int backend_index)const{auto found=positions_.constFind(backend_index);return found==positions_.cend()?QModelIndex{}:index(found.value(),0);}
    QImage image(QModelIndex const& index)const{
        auto item=at(index);if(!item)return {};if(auto cached=images_.object(item->path))return *cached;
        // Only delegates for visible cells call this method. A pending request
        // returns no bitmap; the backend's thumbnail revision triggers repaint.
        auto result=thumbnail?thumbnail(item->index):QImage{};if(result.isNull())return {};
        if(result.width()>512||result.height()>512)result=result.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        images_.insert(item->path,new QImage(result));return result;
    }
    int backend_for_path(QString const& path)const{for(auto const& item:all_)if(item.path==path)return item.index;return -1;}
    int cached_images()const{return images_.size();}
};

class LocalLibraryDelegate final:public QStyledItemDelegate {
public:
    explicit LocalLibraryDelegate(QObject* parent=nullptr):QStyledItemDelegate(parent){}
    QSize sizeHint(QStyleOptionViewItem const&,QModelIndex const&)const override{auto view=qobject_cast<QListView*>(parent());return {view?std::max(1,view->gridSize().width()):180,152};}
    void paint(QPainter* painter,QStyleOptionViewItem const& option,QModelIndex const& index)const override{
        auto model=static_cast<LocalLibraryModel const*>(index.model());auto item=model->at(index);if(!item)return;
        auto image=model->image(index);painter->save();painter->setRenderHint(QPainter::Antialiasing);auto rect=option.rect.adjusted(5,5,-5,-5);bool selected=option.state&QStyle::State_Selected,hover=option.state&QStyle::State_MouseOver;
        painter->setBrush(QColor(state.dark?"#1a212c":"#ffffff"));painter->setPen(QPen(QColor(selected?"#87b7a2":hover?"#607468":state.dark?"#2a3340":"#d8e2dd"),selected?1.5:1));painter->drawRoundedRect(QRectF(rect).adjusted(.5,.5,-.5,-.5),11,11);
        QRect picture=rect.adjusted(7,7,-7,-36);QPainterPath clip;clip.addRoundedRect(picture,7,7);painter->setClipPath(clip,Qt::IntersectClip);
        if(!image.isNull()){QSize scaled=image.size().scaled(picture.size(),Qt::KeepAspectRatioByExpanding);painter->setRenderHint(QPainter::SmoothPixmapTransform);painter->drawImage(QRect(QPoint(picture.center().x()-scaled.width()/2,picture.center().y()-scaled.height()/2),scaled),image);}else painter->fillRect(picture,QColor(state.dark?"#273b37":"#e1eae6"));
        painter->setClipping(false);painter->setFont(option.font);painter->setPen(QColor(state.dark?"#e9edf0":"#26343b"));painter->drawText(QRect(rect.left()+11,rect.bottom()-31,rect.width()-22,25),Qt::AlignVCenter,QFontMetrics(option.font).elidedText(item->title,Qt::ElideRight,rect.width()-22));
        if(option.state&QStyle::State_HasFocus){painter->setBrush(Qt::NoBrush);painter->setPen(QPen(QColor("#87b7a2"),1,Qt::DotLine));painter->drawRoundedRect(QRectF(rect).adjusted(3,3,-3,-3),8,8);}painter->restore();
    }
};

class LocalLibraryView final:public QListView {
    LocalLibraryModel* items_{};
    int chosen_{-1};bool replacing_{},grid_queued_{};
    void geometry_grid(){QSize size(wallpaper_grid_cell_width(this),152);if(size!=gridSize())setGridSize(size);}
    void queue_grid(){if(grid_queued_)return;grid_queued_=true;QTimer::singleShot(0,this,[this]{grid_queued_=false;geometry_grid();});}
    bool eventFilter(QObject* object,QEvent* event)override{if(object==viewport()&&(event->type()==QEvent::Resize||event->type()==QEvent::Show))queue_grid();return QListView::eventFilter(object,event);}
public:
    std::function<void(int)> selected,remove;
    explicit LocalLibraryView(QWidget* parent=nullptr):QListView(parent){
        setObjectName("localWallpaperGrid");setAccessibleName(ui::tr("本地壁纸","Local wallpapers"));setFrameShape(QFrame::NoFrame);setStyleSheet("QListView#localWallpaperGrid {background:transparent;border:0;outline:0;} QToolTip {padding:5px;}");setMinimumHeight(160);setViewMode(QListView::IconMode);setResizeMode(QListView::Adjust);setMovement(QListView::Static);setWrapping(true);setUniformItemSizes(true);setSpacing(0);setSelectionMode(QAbstractItemView::SingleSelection);setEditTriggers(QAbstractItemView::NoEditTriggers);setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setMouseTracking(true);viewport()->setMouseTracking(true);viewport()->setCursor(Qt::PointingHandCursor);
        items_=new LocalLibraryModel(this);setModel(items_);setItemDelegate(new LocalLibraryDelegate(this));
        viewport()->installEventFilter(this);connect(verticalScrollBar(),&QScrollBar::rangeChanged,this,[this]{queue_grid();});
        connect(selectionModel(),&QItemSelectionModel::currentChanged,this,[this](QModelIndex const& current,QModelIndex const&){if(replacing_)return;auto item=items_->at(current);if(item&&chosen_!=item->index){chosen_=item->index;if(selected)selected(item->index);}});
        setContextMenuPolicy(Qt::CustomContextMenu);connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint const& point){auto index=indexAt(point);auto item=items_->at(index);if(!item)return;auto path=item->path;QPointer<LocalLibraryView> self(this);QMenu menu;auto erase=menu.addAction(item->builtin>=0?ui::tr("隐藏内置壁纸","Hide bundled wallpaper"):ui::tr("删除壁纸","Delete wallpaper"));if(menu.exec(viewport()->mapToGlobal(point))==erase&&self&&self->remove){int backend=self->items_->backend_for_path(path);if(backend>=0)self->remove(backend);}});
    }
    void set_items(QList<LocalWallpaper> const& items,int kind,QString const& search,int chosen){replacing_=true;int position=verticalScrollBar()->value();items_->reset(items,kind,search);chosen_=chosen;setCurrentIndex(items_->index_for(chosen));geometry_grid();doItemsLayout();verticalScrollBar()->setValue(std::clamp(position,0,verticalScrollBar()->maximum()));replacing_=false;viewport()->update();}
    void set_selected(int index){chosen_=index;QSignalBlocker block(selectionModel());auto current=items_->index_for(index);selectionModel()->setCurrentIndex(current,QItemSelectionModel::ClearAndSelect);viewport()->update();}
    void thumbnails_changed(){viewport()->update();}
    LocalLibraryModel* items_model()const{return items_;}
    void resizeEvent(QResizeEvent* event)override{QListView::resizeEvent(event);queue_grid();}
    static bool fixture_tests(QString const& directory){
        QDir().mkpath(directory);LocalLibraryView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(670,330);QList<LocalWallpaper> items;items.reserve(10000);for(int n=0;n<10000;++n)items.push_back({n,n%3,-1,QString("Wallpaper %1").arg(n),QString("fixture/%1.png").arg(n)});
        QSet<int> requested;view.items_->thumbnail=[&](int index){requested.insert(index);QImage image(320,180,QImage::Format_RGB32);image.fill(QColor::fromHsv(index%360,130,150));return image;};int chosen=-1;view.selected=[&](int index){chosen=index;};view.set_items(items,-1,{},0);view.show();qApp->processEvents();auto frame=[&]{QImage image(view.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);view.render(&image);return image;};auto first=frame();
        auto settle=[&]{for(int n=0;n<8;++n){qApp->processEvents();view.doItemsLayout();}};
        auto three_columns=[&]{settle();QRect rows[4];for(int n=0;n<4;++n)rows[n]=view.visualRect(view.model()->index(n,0));return rows[0].top()==rows[1].top()&&rows[1].top()==rows[2].top()&&rows[3].top()>rows[0].top()&&rows[0].left()>=0&&rows[2].right()<view.viewport()->width();};
        QJsonObject report;report["ten_thousand_metadata_items"]=view.model()->rowCount()==10000;report["no_card_widgets"]=view.findChildren<QAbstractButton*>().isEmpty();report["initial_thumbnail_requests_bounded"]=requested.size()>0&&requested.size()<32;report["three_columns_fit_scrolled_viewport"]=three_columns();
        view.set_items(items.mid(0,2),-1,{},0);settle();bool hidden=!view.verticalScrollBar()->isVisible();view.set_items(items,-1,{},0);settle();report["scrollbar_appearance_keeps_three_columns"]=hidden&&view.verticalScrollBar()->isVisible()&&three_columns();view.resize(740,330);report["viewport_resize_keeps_three_columns"]=three_columns();first=frame();
        for(int n=0;n<24;++n){view.scrollTo(view.model()->index(n*411,0),QAbstractItemView::PositionAtTop);qApp->processEvents();frame();}
        report["thumbnail_cache_bounded"]=view.items_->cached_images()<=24;report["scrolled_request_count"]=requested.size();view.set_items(items,-1,"Wallpaper 9999",9999);report["search_preserves_backend_index"]=view.model()->rowCount()==1&&view.model()->index(0,0).data(Qt::UserRole).toInt()==9999;
        view.set_selected(-1);view.selectionModel()->setCurrentIndex(view.model()->index(0,0),QItemSelectionModel::ClearAndSelect);report["selection_routes_backend_index"]=chosen==9999;view.set_items(items,1,{},1);bool types=true;for(int n=0;n<view.model()->rowCount();++n)if(view.items_->at(view.model()->index(n,0))->kind!=1){types=false;break;}report["type_filter"]=types;report["metadata_size_compact"]=sizeof(LocalWallpaper)<128;
        bool ok=true;for(auto entry=report.begin();entry!=report.end();++entry)if(entry.value().isBool()&&!entry.value().toBool())ok=false;report["passed"]=ok;first.save(directory+"/local-library-virtualized.png");QSaveFile file(directory+"/local-library-checks.json");auto bytes=QJsonDocument(report).toJson();return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit()&&ok;
    }
};
}
