#pragma once
#include "ui.hpp"
#include <QtConcurrent>
#include <atomic>
#include <optional>

namespace ui {
struct ImageMetadata {QImage image;QSize dimensions;qint64 bytes{};QString extension;bool readable{};};
// One decoder plus one replaceable request, never one queued task per click.
// A decoder already inside a codec is allowed to finish; stale results and all
// queued work are discarded without reading QWidget state from the worker.
class ImageLoader final:public QObject {
    struct Request{QString path;int kind{};QSize bounds;uint64_t generation{};};
    std::shared_ptr<std::atomic<uint64_t>> epoch_=std::make_shared<std::atomic<uint64_t>>(0);
    std::optional<Request> pending_;std::function<void(ImageMetadata)> ready_;bool running_{};unsigned started_{};
    void pump(){
        if(running_||!pending_)return;auto request=std::move(*pending_);pending_.reset();running_=true;++started_;auto epoch=epoch_;auto watcher=new QFutureWatcher<ImageMetadata>(this);
        connect(watcher,&QFutureWatcher<ImageMetadata>::finished,this,[this,watcher,generation=request.generation]{auto result=watcher->result();watcher->deleteLater();running_=false;QPointer<ImageLoader> self(this);if(generation==epoch_->load()&&ready_){auto done=std::move(ready_);done(std::move(result));}if(self)pump();});
        watcher->setFuture(QtConcurrent::run([request=std::move(request),epoch]{ImageMetadata result;if(epoch->load()!=request.generation)return result;QFileInfo file(request.path);result.readable=file.isFile()&&file.isReadable();result.bytes=file.size();result.extension=file.suffix().toUpper();
            if(result.readable&&(request.kind==0||request.kind==1)&&epoch->load()==request.generation){QImageReader reader(request.path);reader.setAutoTransform(true);result.dimensions=reader.size();if(request.kind==0&&epoch->load()==request.generation&&result.dimensions.isValid()&&qint64(result.dimensions.width())*result.dimensions.height()<=80*1024*1024){reader.setScaledSize(result.dimensions.scaled(request.bounds,Qt::KeepAspectRatio));result.image=reader.read();}}return result;}));
    }
public:
    explicit ImageLoader(QObject* parent=nullptr):QObject(parent){}
    ~ImageLoader()override{cancel();}
    void request(QString path,int kind,QSize bounds,std::function<void(ImageMetadata)> ready){auto generation=epoch_->fetch_add(1)+1;pending_=Request{std::move(path),kind,bounds,generation};ready_=std::move(ready);pump();}
    void cancel(){epoch_->fetch_add(1);pending_.reset();ready_={};}
    static bool fixture_tests(QString const& directory){
        QDir().mkpath(directory);QString file=directory+"/coalesced-image.png";QImage sample(640,360,QImage::Format_RGB32);sample.fill(QColor("#467a72"));bool ok=sample.save(file);ImageLoader loader;int callbacks=0;QString final;
        for(int n=0;n<100;++n)loader.request(n==99?file:file+".missing",0,{320,180},[&,n](ImageMetadata result){++callbacks;final=QString::number(n);ok=ok&&result.readable&&result.image.size()==QSize(320,180);});
        QElapsedTimer timer;timer.start();while((loader.running_||loader.pending_)&&timer.elapsed()<10000){qApp->processEvents(QEventLoop::ExcludeUserInputEvents);QThread::msleep(1);}bool latest=callbacks==1&&final=="99"&&loader.started_<=2&&!loader.running_;int before=callbacks;loader.request(file,0,{320,180},[&](ImageMetadata){++callbacks;});loader.cancel();timer.restart();while(loader.running_&&timer.elapsed()<10000){qApp->processEvents(QEventLoop::ExcludeUserInputEvents);QThread::msleep(1);}bool cancel=callbacks==before&&!loader.running_&&!loader.pending_;ok=ok&&latest&&cancel;
        QJsonObject result{{"passed",ok},{"hundred_requests_latest_only",latest},{"cancel_has_no_callback",cancel},{"max_running",1},{"max_pending",1}};QSaveFile report(directory+"/image-loader-checks.json");auto bytes=QJsonDocument(result).toJson();return report.open(QIODevice::WriteOnly)&&report.write(bytes)==bytes.size()&&report.commit()&&ok;
    }
};
}
