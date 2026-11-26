#pragma once
#include "ui.hpp"
#include <QtConcurrent>
#include <QScopeGuard>

namespace ui::storage {
struct Candidate {QString folder,name;qint64 bytes,modified;};
struct Usage {
    qint64 program{},wallpapers{},recordings{},clipboard{},previews{},covers{},reclaimable{},removed{};
    QList<Candidate> candidates;
    bool incomplete{};
};
inline bool ordinary(QString const& path){
    auto attributes=GetFileAttributesW(w(QDir::toNativeSeparators(path)));
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
inline qint64 measure(QString const& root,QStringList const& skip,Usage& result){
    if(!QFileInfo::exists(root)||!ordinary(root))return 0;
    qint64 bytes=0;
    QList<QString> directories{root};
    while(!directories.isEmpty()){
        QDir dir(directories.takeLast());
        if(!dir.isReadable()){result.incomplete=true;continue;}
        for(auto const& info:dir.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::NoSort)){
            auto path=info.absoluteFilePath();
            if(skip.contains(path,Qt::CaseInsensitive)||!ordinary(path))continue;
            if(info.isDir())directories.append(path);
            else if(info.isFile())bytes+=info.size();
        }
    }
    return bytes;
}
inline bool cache_name(QString const& folder,QString const& name){
    static QRegularExpression local("^[0-9]{1,20}[.]png$"),online("^[0-9a-f]{64}[.]png$");
    return (folder=="thumbnails"&&local.match(name).hasMatch())||(folder=="online/covers"&&online.match(name).hasMatch());
}
inline Usage scan(QString install,QString profile){
    Usage result;
    install=QDir(install).absolutePath();profile=QDir(profile).absolutePath();
    result.program=measure(install,{profile,install+"/data"},result);
    result.wallpapers=measure(profile+"/media",{},result)+measure(profile+"/online",{profile+"/online/covers"},result);
    result.recordings=measure(profile+"/recordings",{},result);
    result.clipboard=measure(profile+"/clipboard",{},result);
    result.previews=measure(profile+"/previews",{},result);
    for(QString const& folder:{QString("thumbnails"),QString("online/covers")}){
        auto path=profile+"/"+folder;
        if(!ordinary(profile)||(folder.startsWith("online/")&&!ordinary(profile+"/online"))||!ordinary(path))continue;
        result.covers+=measure(path,{},result);
        for(auto const& file:QDir(path).entryInfoList(QDir::Files|QDir::NoSymLinks,QDir::NoSort)){
            if(!cache_name(folder,file.fileName())||!ordinary(file.absoluteFilePath()))continue;
            result.candidates.append({folder,file.fileName(),file.size(),file.lastModified().toMSecsSinceEpoch()});
            result.reclaimable+=file.size();
        }
    }
    return result;
}
// Hold the directories against replacement while deleting the inspected file by handle.
inline bool remove_cover(QString const& root,Candidate const& file){
    if(!cache_name(file.folder,file.name))return false;
    QList<HANDLE> held;
    auto cleanup=qScopeGuard([&]{for(auto h:held)CloseHandle(h);});
    auto hold=[&](QString const& path,bool directory){
        DWORD flags=FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0);
        HANDLE h=CreateFileW(w(QDir::toNativeSeparators(path)),FILE_READ_ATTRIBUTES|(directory?0:DELETE),
            directory?FILE_SHARE_READ|FILE_SHARE_WRITE:FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
        if(h==INVALID_HANDLE_VALUE)return false;
        held.append(h);BY_HANDLE_FILE_INFORMATION info{};
        return GetFileInformationByHandle(h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory;
    };
    if(!hold(root,true))return false;
    QString path=root;
    for(auto const& part:file.folder.split('/')){path+="/"+part;if(!hold(path,true))return false;}
    path+="/"+file.name;
    if(!hold(path,false))return false;
    QFileInfo current(path);
    if(current.size()!=file.bytes||current.lastModified().toMSecsSinceEpoch()!=file.modified)return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(held.last(),FileDispositionInfo,&disposition,sizeof disposition);
}
inline bool fixture_tests(){
    QTemporaryDir temp;
    if(!temp.isValid())return false;
    auto profile=temp.path()+"/data/profile-v2";
    auto create=[](QString const& path){
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write("1234",4)==4;
    };
    for(auto name:{"media/photo.jpg","recordings/result.mp4","clipboard/history.sqlite3","previews/preview.gif",
                   "online/movie.mp4","thumbnails/123.png","thumbnails/keep.png"})
        if(!create(profile+"/"+name))return false;
    if(!create(temp.path()+"/program.exe"))return false;
    auto usage=scan(temp.path(),profile);
    if(usage.program!=4||usage.wallpapers!=8||usage.recordings!=4||usage.clipboard!=4||usage.previews!=4||
       usage.covers!=8||usage.reclaimable!=4||usage.candidates.size()!=1)return false;
    auto removed=remove_cover(profile,usage.candidates.first());
    return removed&&scan(temp.path(),profile).reclaimable==0&&QFile::exists(profile+"/thumbnails/keep.png")&&
        QFile::exists(profile+"/media/photo.jpg")&&!remove_cover(profile,{"../media","photo.jpg",4,0});
}
class Panel final:public QWidget {
    QLabel *details_{},*status_{};QPushButton *refresh_{},*clean_{};
    Usage usage_;
    bool busy_{};
    void run(bool remove){
        if(busy_)return;
        busy_=true;refresh_->setEnabled(false);clean_->setEnabled(false);
        status_->setText(remove?ui::tr("正在清理封面缓存…","Clearing cached covers…"):ui::tr("正在计算占用…","Measuring storage…"));
        auto task=new QFutureWatcher<Usage>(this);
        connect(task,&QFutureWatcher<Usage>::finished,this,[this,task,remove]{
            usage_=task->result();task->deleteLater();busy_=false;
            auto format=[](qint64 bytes){return QLocale().formattedDataSize(bytes);};
            QStringList lines;
            for(auto const& row:QList<QPair<QString,qint64>>{
                {ui::tr("程序文件","Application"),usage_.program},{ui::tr("本地与在线壁纸","Wallpaper library"),usage_.wallpapers},
                {ui::tr("录屏工作文件","Recording workspace"),usage_.recordings},{ui::tr("剪贴板历史","Clipboard history"),usage_.clipboard},
                {ui::tr("在线预览缓存","Online previews"),usage_.previews},{ui::tr("图库封面缓存","Wallpaper covers"),usage_.covers}})
                lines.append(row.first+"   "+format(row.second));
            details_->setText(lines.join('\n'));
            auto message=ui::tr("可清理的封面缓存为 %1","Reclaimable covers: %1").arg(format(usage_.reclaimable));
            if(remove)message=ui::tr("已清理 %1。","Freed %1. ").arg(format(usage_.removed))+message;
            if(usage_.incomplete)message+=" "+ui::tr("部分目录无法读取。","Some directories could not be read.");
            status_->setText(message);refresh_->setEnabled(true);clean_->setEnabled(usage_.reclaimable>0);
        });
        auto install=s(state.directory),profile=s(state.data);auto candidates=remove?usage_.candidates:QList<Candidate>{};
        task->setFuture(QtConcurrent::run([install,profile,candidates]{
            qint64 removed=0;
            for(auto const& file:candidates)if(remove_cover(profile,file))removed+=file.bytes;
            auto result=scan(install,profile);result.removed=removed;return result;
        }));
    }
public:
    explicit Panel(QWidget* owner=nullptr):QWidget(owner){
        auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        details_=label("");status_=label(ui::tr("统计当前安装和应用数据目录。","Measures this installation and its app data."),"muted",true);
        layout->addWidget(details_);layout->addWidget(status_);
        auto actions=new QHBoxLayout;refresh_=button(ui::tr("查看空间占用","Measure storage"),[this]{run(false);});
        clean_=button(ui::tr("清理封面缓存","Clear cached covers"),[this]{
            auto bytes=QLocale().formattedDataSize(usage_.reclaimable);
            if(QMessageBox::question(this,ui::tr("清理封面缓存","Clear cached covers"),ui::tr("清理最多 %1 的封面缓存？图库可自动生成封面。壁纸、录屏和历史记录会保留。","Clear up to %1 of regenerable covers? Wallpapers, recordings and history will be kept.").arg(bytes))==QMessageBox::Yes)run(true);
        });
        clean_->setEnabled(false);actions->addWidget(refresh_);actions->addWidget(clean_);
        actions->addWidget(button(ui::tr("打开数据目录","Open data folder"),[]{QDesktopServices::openUrl(QUrl::fromLocalFile(s(state.data)));}));layout->addLayout(actions);
    }
};
}
