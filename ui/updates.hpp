#pragma once
#include "ui.hpp"
#include "../src/version.hpp"
#include <QtNetwork>
#include <appmodel.h>
#include <functional>
#include <algorithm>
#include <cmath>

namespace ui {
class UpdateService final : public QObject {
public:
    enum class Phase { Idle, Checking, Latest, Available, Downloading, Preparing, Installing, Error };
    static constexpr qint64 maximum_setup=128ll*1024*1024;
    struct Release {QString tag,digest,url; qint64 size{};};
private:
    static bool packaged_process() {
        UINT32 length=0;
        return GetCurrentPackageFullName(&length,nullptr)==ERROR_INSUFFICIENT_BUFFER;
    }
    bool store_managed_=packaged_process();
    bool store_updates() const {return store_managed_&&!fixture_;}
    QNetworkAccessManager network_{this};QNetworkReply* reply_{};QByteArray body_,prefix_;
    std::unique_ptr<QSaveFile> file_;QCryptographicHash hash_{QCryptographicHash::Sha256};
    QTimer speed_,prepare_;QElapsedTimer elapsed_,prepared_clock_;qint64 activity_{},received_{},total_{};
    Release release_;Phase phase_=Phase::Idle;QString error_,source_,setup_,helper_copy_;int source_index_{};
    HANDLE helper_{},ready_event_{};bool committed_{},fixture_{};QDateTime checked_;
    QList<QPair<QString,QString>> fixture_sources_;
    QStringList fixture_metadata_,metadata_errors_;
    static QString repository(){return "https://github.com/GalokPeng/light-block-desktop";}
    static QString asset_url(QString tag){return repository()+"/releases/download/"+tag+"/Qingkuai-Setup-x64.exe";}
    static QString quote(QString const& input){QString out="\"";int slash=0;for(auto ch:input){if(ch=='\\'){++slash;continue;}out+=QString(slash*(ch=='"'?2:1)+(ch=='"'?1:0),'\\');slash=0;out+=ch;}return out+QString(slash*2,'\\')+'"';}
    static bool version(QString const& text,QList<int>& parts){static QRegularExpression re("^v([0-9]{1,5})\\.([0-9]{1,5})\\.([0-9]{1,5})$");auto match=re.match(text);if(!match.hasMatch())return false;parts={match.captured(1).toInt(),match.captured(2).toInt(),match.captured(3).toInt()};return true;}
    static bool digest(QString const& value){return QRegularExpression("^[a-f0-9]{64}$").match(value).hasMatch();}
    static bool parse_size(QJsonValue const& value,qint64& out,bool optional=false){
        out=0;if(optional&&(value.isNull()||value.isUndefined()))return true;
        double size=value.toDouble(-1);if(!value.isDouble()||!std::isfinite(size)||size<2||size>maximum_setup||std::floor(size)!=size)return false;
        out=qint64(size);return true;
    }
    static bool parse_site(QByteArray const& bytes,Release& out){
        if(bytes.size()>64*1024)return false;auto doc=QJsonDocument::fromJson(bytes);if(!doc.isObject())return false;
        auto obj=doc.object();QList<int> parts;Release next{obj["version"].toString(),obj["sha256"].toString(),obj["url"].toString(),0};
        // The official checksum feed historically returned null size. The stream
        // remains bounded and SHA-256 verified even when its length is unknown.
        if(!version(next.tag,parts)||!digest(next.digest)||obj["name"].toString()!="Qingkuai-Setup-x64.exe"||next.url!=asset_url(next.tag)||!parse_size(obj["size"],next.size,true))return false;
        out=next;return true;
    }
    void discard_reply(){if(reply_){auto old=reply_;reply_=nullptr;old->disconnect(this);old->abort();old->deleteLater();}}
    void discard_file(){if(file_)file_->cancelWriting();file_.reset();}
    void fail(QString message){discard_reply();discard_file();speed_.stop();phase_=Phase::Error;error_=std::move(message);checked_=QDateTime::currentDateTimeUtc();}
    void publish_release(){QList<int> latest,current;if(!version(release_.tag,latest)||!version(QString("v" LB_VERSION_UTF8).section('-',0,0),current)){fail(ui::tr("版本信息无效","Invalid version information"));return;}phase_=std::lexicographical_compare(current.begin(),current.end(),latest.begin(),latest.end())?Phase::Available:Phase::Latest;checked_=QDateTime::currentDateTimeUtc();error_.clear();}
    QString metadata_target(int stage)const{
        if(fixture_)return fixture_metadata_.value(stage);
        QString primary="https://lightblock.galok.cn/download/?format=json",alternate="https://light-block-desktop.vercel.app/download/?format=json";
        if(state.english)std::swap(primary,alternate);
        if(stage<2)return stage==0?primary:alternate;
        if(stage==2)return "https://api.github.com/repos/GalokPeng/light-block-desktop/releases/latest";
        return repository()+"/releases/download/"+release_.tag+"/Qingkuai-"+release_.tag+"-SHA256SUMS.txt";
    }
    void metadata_failed(int stage,QString detail){
        discard_reply();QString reason=QUrl(metadata_target(stage)).host()+": "+detail;
        metadata_errors_.append(reason);qWarning().noquote()<<"Update check:"<<reason;
        if(stage<2)metadata(stage+1);
        else fail(ui::tr("暂时无法检查更新，点击重试","Could not check for updates. Click to retry.")+"\n"+metadata_errors_.join('\n'));
    }
    void metadata(int stage){
        body_.clear();QNetworkRequest request{QUrl(metadata_target(stage))};
        // Allow a cold official relay to finish its bounded upstream fallback.
        // Also impose a wall-clock deadline so trickled bytes cannot hang checks.
        int timeout=fixture_?150:22000;request.setTransferTimeout(timeout);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader("User-Agent","Light-Block-Desktop/" LB_VERSION_UTF8);
        request.setRawHeader("Accept",stage==2?"application/vnd.github+json":"*/*");
        request.setRawHeader("Cache-Control","no-cache");
        auto reply=network_.get(request);reply_=reply;reply->setReadBufferSize(64*1024);
        auto deadline=new QTimer(reply);deadline->setSingleShot(true);
        connect(deadline,&QTimer::timeout,this,[this,reply,stage]{if(reply_==reply)metadata_failed(stage,ui::tr("连接超时","Connection timed out"));});deadline->start(timeout);
        connect(reply,&QNetworkReply::readyRead,this,[this,reply,stage]{
            if(reply_!=reply)return;body_+=reply->readAll();
            if(body_.size()>512*1024)metadata_failed(stage,ui::tr("更新信息过大","Update metadata is too large"));
        });
        connect(reply,&QNetworkReply::finished,this,[this,reply,deadline,stage]{
            if(reply_!=reply)return;deadline->stop();body_+=reply->readAll();
            int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool ok=reply->error()==QNetworkReply::NoError&&status==200;
            auto detail=status?QString("HTTP %1").arg(status):reply->errorString();
            reply_=nullptr;reply->deleteLater();
            if(!ok){metadata_failed(stage,detail);return;}
            if(body_.size()>512*1024){metadata_failed(stage,ui::tr("更新信息过大","Update metadata is too large"));return;}
            if(stage<2){if(parse_site(body_,release_))publish_release();else metadata_failed(stage,ui::tr("更新信息格式无效","Invalid update metadata"));return;}
            if(stage==3){
                QString found;for(auto line:QString::fromLatin1(body_).split('\n')){
                    auto fields=line.trimmed().split(QRegularExpression("\\s+"));
                    if(fields.size()==2&&fields[1]=="Qingkuai-Setup-x64.exe"&&digest(fields[0])){
                        if(!found.isEmpty()){metadata_failed(stage,ui::tr("更新校验信息无效","Invalid update checksum"));return;}found=fields[0];
                    }
                }
                if(found.isEmpty()){metadata_failed(stage,ui::tr("缺少官方安装包校验值","Official installer checksum is missing"));return;}
                release_.digest=found;publish_release();return;
            }
            auto doc=QJsonDocument::fromJson(body_);auto object=doc.object();Release candidate;QList<int> parts;
            candidate.tag=object["tag_name"].toString();
            if(!doc.isObject()||object["draft"].toBool()||object["prerelease"].toBool()||!version(candidate.tag,parts)){
                metadata_failed(stage,ui::tr("发布信息无效","Invalid release metadata"));return;
            }
            int matches=0;for(auto item:object["assets"].toArray()){
                auto asset=item.toObject();if(asset["name"].toString()!="Qingkuai-Setup-x64.exe")continue;++matches;
                candidate.url=asset["browser_download_url"].toString();
                if(!parse_size(asset["size"],candidate.size))candidate.size=-1;
                auto hash=asset["digest"].toString();if(hash.startsWith("sha256:"))candidate.digest=hash.mid(7);
            }
            if(matches!=1||candidate.url!=asset_url(candidate.tag)||candidate.size<2){metadata_failed(stage,ui::tr("官方安装包信息无效","Official installer information is invalid"));return;}
            release_=candidate;if(digest(candidate.digest))publish_release();else metadata(3);
        });
    }
    QList<QPair<QString,QString>> sources()const{if(fixture_)return fixture_sources_;QList<QPair<QString,QString>> official{{"GitHub",release_.url}},mirrors{{"gh-proxy.com","https://gh-proxy.com/"+release_.url},{"ghfast.top","https://ghfast.top/"+release_.url}};return state.english?official+mirrors:mirrors+official;}
    void consume(QNetworkReply* reply){if(reply_!=reply)return;auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();if(status>=300&&status<400)return;if(status!=200){next_source();return;}bool parsed=true;QByteArray length=reply->rawHeader("Content-Length");qint64 declared=length.isEmpty()?0:length.toLongLong(&parsed);if(!parsed||declared<0||declared>maximum_setup||(declared&&release_.size&&declared!=release_.size)){next_source();return;}total_=release_.size?release_.size:declared;while(reply_==reply&&reply->bytesAvailable()){auto chunk=reply->read(128*1024);if(chunk.isEmpty())break;prefix_=(prefix_+chunk).left(2);if((prefix_.size()==2&&prefix_!="MZ")||chunk.size()>maximum_setup-received_){next_source();return;}if(!file_||file_->write(chunk)!=chunk.size()){fail(ui::tr("无法保存更新，请检查磁盘空间","Cannot save the update. Check disk space."));return;}hash_.addData(chunk);received_+=chunk.size();activity_=elapsed_.elapsed();}}
    void next_source(){discard_reply();discard_file();speed_.stop();if(++source_index_<sources().size())attempt();else fail(ui::tr("安装包下载失败，可点击重试","Installer download failed. Click to retry."));}
    void attempt(){
        auto list=sources();if(source_index_>=list.size()){fail(ui::tr("没有可用下载源","No download source is available"));return;}received_=0;total_=release_.size;prefix_.clear();hash_.reset();source_=list[source_index_].first;phase_=Phase::Downloading;file_=std::make_unique<QSaveFile>(setup_);file_->setDirectWriteFallback(false);if(!file_->open(QIODevice::WriteOnly)){fail(ui::tr("无法写入更新目录","Cannot write to the update folder"));return;}
        QNetworkRequest request{QUrl(list[source_index_].second)};request.setTransferTimeout(12000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);request.setRawHeader("Accept-Encoding","identity");request.setRawHeader("User-Agent","Light-Block-Desktop/" LB_VERSION_UTF8);auto reply=network_.get(request);reply_=reply;reply->setReadBufferSize(256*1024);elapsed_.restart();activity_=0;speed_.start();connect(reply,&QNetworkReply::metaDataChanged,this,[this,reply]{consume(reply);});connect(reply,&QNetworkReply::readyRead,this,[this,reply]{consume(reply);});
        connect(reply,&QNetworkReply::finished,this,[this,reply]{if(reply_!=reply)return;consume(reply);if(reply_!=reply)return;if(reply->error()!=QNetworkReply::NoError||received_<2||(total_&&received_!=total_)||prefix_!="MZ"||hash_.result().toHex()!=release_.digest.toLatin1()){next_source();return;}reply_=nullptr;reply->deleteLater();speed_.stop();if(!file_||!file_->commit()){fail(ui::tr("更新文件保存失败","Could not save the update"));return;}file_.reset();prepare_install();});
    }
    QString installed_directory()const{QString directory=s(state.directory);for(auto name:QDir(directory).entryList({"unins*.dat"},QDir::Files)){QFile file(directory+"/"+name);if(!QFileInfo::exists(directory+"/"+QFileInfo(name).completeBaseName()+".exe")||!file.open(QIODevice::ReadOnly))continue;auto data=file.read(8192);if(data.startsWith("Inno Setup Uninstall Log")&&data.contains(LB_INSTALLER_APP_ID))return directory;}return {};}
    void clear_helper(bool terminate){prepare_.stop();if(helper_){if(terminate&&!committed_&&WaitForSingleObject(helper_,0)==WAIT_TIMEOUT)TerminateProcess(helper_,1);CloseHandle(helper_);helper_=nullptr;}if(ready_event_){CloseHandle(ready_event_);ready_event_=nullptr;}}
    void prepare_install(){
        phase_=Phase::Preparing;if(fixture_)return;QString identity=QUuid::createUuid().toString(QUuid::Id128);helper_copy_=QFileInfo(setup_).absolutePath()+"/QingkuaiUpdate-"+identity+".exe";if(!QFile::copy(s(state.directory)+"/runtime/QingkuaiUpdate.exe",helper_copy_)){fail(ui::tr("更新组件缺失，请重新下载安装包","Update helper is missing. Download Setup again."));return;}QString event="Local\\Qingkuai.Update."+identity;ready_event_=CreateEventW(nullptr,TRUE,FALSE,w(event));if(!ready_event_){fail(ui::tr("无法准备安装更新","Cannot prepare the update"));return;}QString directory=installed_directory();QString profile=QDir::cleanPath(s(state.data));QString forwarded=!directory.isEmpty()&&profile==QDir::cleanPath(directory+"/data")?QString{}:profile;QStringList args{"--parent",QString::number(GetCurrentProcessId()),"--setup",setup_,"--sha256",release_.digest,"--directory",directory,"--profile",forwarded,"--language",state.english?"english":"chinesesimplified","--mode",directory.isEmpty()?"interactive":"installed","--ready",event};QString command=quote(helper_copy_);for(auto const& arg:args)command+=' '+quote(arg);auto mutable_command=command.toStdWString();STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};if(!CreateProcessW(w(helper_copy_),mutable_command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){clear_helper(false);fail(ui::tr("无法启动更新组件","Cannot start the update helper"));return;}CloseHandle(process.hThread);helper_=process.hProcess;committed_=false;prepared_clock_.restart();prepare_.start();
    }
public:
    UpdateService(){speed_.setInterval(1000);connect(&speed_,&QTimer::timeout,this,[this]{auto time=elapsed_.elapsed();bool stalled=time-activity_>(received_?12000:6000);bool slow=time>=10000&&received_<2*1024*1024&&received_*1000/std::max<qint64>(1,time)<64*1024;if(stalled||(slow&&source_index_+1<sources().size()))next_source();});prepare_.setInterval(50);connect(&prepare_,&QTimer::timeout,this,[this]{if(!helper_||WaitForSingleObject(helper_,0)!=WAIT_TIMEOUT){clear_helper(false);fail(ui::tr("更新组件未能就绪","Update helper did not become ready"));return;}if(ready_event_&&WaitForSingleObject(ready_event_,0)==WAIT_OBJECT_0){prepare_.stop();CloseHandle(ready_event_);ready_event_=nullptr;committed_=true;phase_=Phase::Installing;api.action(900);}else if(prepared_clock_.elapsed()>15000){clear_helper(true);fail(ui::tr("准备更新超时，请重试","Preparing the update timed out. Try again."));}});}
    ~UpdateService()override{discard_reply();discard_file();clear_helper(!committed_);}
    void check(bool force=false){if(store_updates())return;if(LB_PREVIEW_BUILD&&!fixture_)return;if(phase_==Phase::Checking||busy())return;if(!force&&checked_.isValid()&&checked_.secsTo(QDateTime::currentDateTimeUtc())<3600)return;phase_=Phase::Checking;error_.clear();metadata_errors_.clear();release_={};metadata(0);}
    bool busy()const{return phase_==Phase::Downloading||phase_==Phase::Preparing||phase_==Phase::Installing;}
    void cancel(){if(phase_==Phase::Installing)return;discard_reply();discard_file();speed_.stop();clear_helper(true);phase_=release_.tag.isEmpty()?Phase::Idle:Phase::Available;error_.clear();}
    void quit_failed(){if(phase_!=Phase::Installing)return;committed_=false;clear_helper(true);fail(ui::tr("请先完成当前编辑，再重试更新","Finish the current edit before retrying the update"));}
    void click(){if(store_updates()){QDesktopServices::openUrl(QUrl("ms-windows-store://downloadsandupdates"));return;}if(LB_PREVIEW_BUILD&&!fixture_){QDesktopServices::openUrl(QUrl("https://light-block-desktop.vercel.app/preview/"));return;}if(phase_==Phase::Downloading){cancel();return;}if(busy()||phase_==Phase::Checking)return;if(phase_!=Phase::Available){check(true);return;}QString directory=s(state.data)+"/updates";if(!QDir().mkpath(directory)){fail(ui::tr("无法创建更新目录","Cannot create the update folder"));return;}setup_=QDir(directory).absoluteFilePath("Qingkuai-"+release_.tag+"-Setup-x64.exe");source_index_=0;attempt();}
    QString text()const{if(store_updates())return ui::tr("在 Microsoft Store 中更新","Update in Microsoft Store");if(LB_PREVIEW_BUILD&&!fixture_)return ui::tr("测试版说明","Preview information");switch(phase_){case Phase::Checking:return ui::tr("正在检查更新…","Checking for updates…");case Phase::Latest:return ui::tr("已是最新版本","Up to date");case Phase::Available:return ui::tr("更新至 ","Update to ")+release_.tag;case Phase::Downloading:return ui::tr("更新下载 %1%","Downloading %1%").arg(total_?std::min<qint64>(100,received_*100/total_):0);case Phase::Preparing:return ui::tr("正在准备安装…","Preparing update…");case Phase::Installing:return ui::tr("正在退出并更新…","Closing to update…");case Phase::Error:return ui::tr("检查更新 · 重试","Check updates · Retry");default:return ui::tr("检查更新","Check for updates");}}
    QString tooltip()const{if(store_updates())return ui::tr("打开 Microsoft Store 更新页面","Open updates in Microsoft Store");if(LB_PREVIEW_BUILD&&!fixture_)return ui::tr("测试版不参与正式版自动更新","Preview builds do not participate in stable updates");return !error_.isEmpty()?error_:phase_==Phase::Downloading?ui::tr("来源：%1 · 点击取消","Source: %1 · Click to cancel").arg(source_):ui::tr("从官方发布检查并安装更新","Check and install an official update");}
    bool enabled()const{return phase_!=Phase::Checking&&phase_!=Phase::Preparing&&phase_!=Phase::Installing;}
    static bool tests(QString const& directory) {
        if(!QDir().mkpath(directory))return false;QJsonObject result;Release out;
        UpdateService store;store.store_managed_=true;store.check(true);
        result["store_channel_delegates_updates"]=store.phase_==Phase::Idle&&!store.reply_&&store.text().contains("Microsoft Store");
        UpdateService production;if(LB_PREVIEW_BUILD)production.check(true);result["preview_no_update_request"]=!LB_PREVIEW_BUILD||(production.phase_==Phase::Idle&&!production.reply_);
        QJsonObject valid{{"version","v1.1.1"},{"sha256",QString(64,'a')},{"name","Qingkuai-Setup-x64.exe"},{"url",asset_url("v1.1.1")},{"size",4096}};
        auto serialize=[](QJsonObject const& value){return QJsonDocument(value).toJson(QJsonDocument::Compact);};
        result["official_metadata"]=parse_site(serialize(valid),out);auto invalid=valid;invalid["url"]="https://example.com/setup.exe";result["foreign_installer_rejected"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["size"]=double(maximum_setup+1);result["old_updater_size_limit"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["version"]="v1.1.2/../file";result["invalid_version_rejected"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["sha256"]=QString(64,'z');result["invalid_digest_rejected"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["size"]=4096.5;result["fractional_size_rejected"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["size"]=QJsonValue::Null;result["legacy_null_size_metadata"]=parse_site(serialize(invalid),out)&&out.size==0;
        invalid.remove("size");result["missing_size_metadata"]=parse_site(serialize(invalid),out)&&out.size==0;
        invalid=valid;invalid["size"]="4096";result["string_size_rejected"]=!parse_site(serialize(invalid),out);
        invalid=valid;invalid["size"]=0;result["zero_size_rejected"]=!parse_site(serialize(invalid),out);
        UpdateService fixture;fixture.fixture_=true;fixture.release_=out;fixture.release_.tag="v99999.0.0";fixture.publish_release();result["semantic_version_comparison"]=fixture.phase_==Phase::Available;
        QString argument=QStringLiteral("a b")+QChar(92);auto quoted=(QStringLiteral("test.exe ")+quote(argument)).toStdWString();int argc=0;wchar_t** argv=CommandLineToArgvW(quoted.c_str(),&argc);result["arguments_are_quoted"]=argv&&argc==2&&QString::fromWCharArray(argv[1])==argument;if(argv)LocalFree(argv);
        QTcpServer server;bool listening=server.listen(QHostAddress::LocalHost,0);result["loopback_server"]=listening;
        QByteArray payload(384*1024,'x');payload[0]='M';payload[1]='Z';QString payload_hash=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());QStringList requests;
        QObject::connect(&server,&QTcpServer::newConnection,&server,[&]{while(auto socket=server.nextPendingConnection()){
            socket->setProperty("request",QByteArray{});QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{if(socket->property("served").toBool())return;QByteArray request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);if(!request.contains("\r\n\r\n"))return;socket->setProperty("served",true);auto path=QString::fromLatin1(request.split(' ').value(1));requests.append(path);
                if(path.startsWith("/meta/")){
                    if(path=="/meta/hang")return;
                    QByteArray data;int status=200;auto object=valid;object["version"]="v99999.0.0";object["url"]=asset_url("v99999.0.0");
                    if(path=="/meta/null")object["size"]=QJsonValue::Null;
                    if(path=="/meta/foreign")object["url"]="https://foreign.example/setup.exe";
                    if(path=="/meta/current"){object["version"]="v" LB_VERSION_UTF8;object["url"]=asset_url("v" LB_VERSION_UTF8);}
                    data=serialize(object);
                    if(path=="/meta/fail"){status=503;data="Unavailable";}
                    if(path=="/meta/html")data="<html>Sign in to network</html>";
                    if(path=="/meta/huge")data=QByteArray(600*1024,'x');
                    if(path=="/meta/api"||path=="/meta/api-checksum"){
                        QJsonObject asset{{"name","Qingkuai-Setup-x64.exe"},{"browser_download_url",asset_url("v99999.0.0")},{"size",4096}};
                        if(path=="/meta/api")asset["digest"]="sha256:"+QString(64,'a');
                        data=serialize(QJsonObject{{"tag_name","v99999.0.0"},{"draft",false},{"prerelease",false},{"assets",QJsonArray{asset}}});
                    }
                    if(path=="/meta/checksum")data=QByteArray(64,'a')+"  Qingkuai-Setup-x64.exe\n";
                    socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nContent-Length: "+QByteArray::number(data.size())+"\r\nConnection: close\r\n\r\n"+data);socket->disconnectFromHost();return;
                }
                if(path=="/oversize"){socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(maximum_setup+1)+"\r\nConnection: close\r\n\r\n");socket->flush();return;}
                if(path=="/unknown"){socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"+payload);socket->disconnectFromHost();return;}
                auto data=std::make_shared<QByteArray>(payload);if(path=="/bad")(*data)[10]='b';if(path=="/html")data->replace(0,2,"<h");socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(data->size())+"\r\nConnection: close\r\n\r\n");auto timer=new QTimer(socket);timer->setInterval(2);auto offset=std::make_shared<qsizetype>(0);QObject::connect(timer,&QTimer::timeout,socket,[socket,timer,data,offset]{if(socket->state()!=QAbstractSocket::ConnectedState){timer->stop();return;}auto count=std::min<qsizetype>(16384,data->size()-*offset);socket->write(data->constData()+*offset,count);*offset+=count;if(*offset==data->size()){timer->stop();socket->disconnectFromHost();}});timer->start();
            });
        }});
        auto base=QString("http://127.0.0.1:%1").arg(server.serverPort());
        auto configure=[&](QString name,QStringList paths){fixture.cancel();fixture.release_={"v99999.0.0",payload_hash,asset_url("v99999.0.0"),payload.size()};fixture.setup_=directory+"/"+name;fixture.fixture_sources_.clear();for(auto const& path:paths)fixture.fixture_sources_.append({path,base+path});fixture.source_index_=0;fixture.attempt();};
        auto wait=[&](std::function<bool()> done,int timeout=5000){QElapsedTimer clock;clock.start();while(!done()&&clock.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}return done();};
        auto bytes=[](QString path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};};
        auto check_metadata=[&](QStringList paths){fixture.cancel();fixture.fixture_metadata_.clear();for(auto path:paths)fixture.fixture_metadata_.append(base+path);fixture.check(true);return wait([&]{return fixture.phase_!=Phase::Checking;});};
        if(listening){
            requests.clear();result["null_size_does_not_require_github"]=check_metadata({"/meta/null","/meta/fail","/meta/fail"})&&fixture.phase_==Phase::Available&&fixture.release_.size==0&&requests==QStringList{"/meta/null"};
            requests.clear();result["site_failure_uses_alternate"]=check_metadata({"/meta/fail","/meta/valid","/meta/fail"})&&fixture.phase_==Phase::Available&&requests==QStringList{"/meta/fail","/meta/valid"};
            result["html_response_falls_back"]=check_metadata({"/meta/html","/meta/valid","/meta/fail"})&&fixture.phase_==Phase::Available;
            result["foreign_metadata_falls_back"]=check_metadata({"/meta/foreign","/meta/valid","/meta/fail"})&&fixture.phase_==Phase::Available;
            result["metadata_size_limit_falls_back"]=check_metadata({"/meta/huge","/meta/valid","/meta/fail"})&&fixture.phase_==Phase::Available;
            result["metadata_timeout_falls_back"]=check_metadata({"/meta/hang","/meta/valid","/meta/fail"})&&fixture.phase_==Phase::Available;
            result["github_api_fallback"]=check_metadata({"/meta/fail","/meta/fail","/meta/api"})&&fixture.phase_==Phase::Available;
            result["github_checksum_fallback"]=check_metadata({"/meta/fail","/meta/fail","/meta/api-checksum","/meta/checksum"})&&fixture.phase_==Phase::Available&&fixture.release_.digest==QString(64,'a');
            result["failed_check_has_diagnostics"]=check_metadata({"/meta/fail","/meta/fail","/meta/fail"})&&fixture.phase_==Phase::Error&&fixture.tooltip().contains("HTTP 503")&&fixture.metadata_errors_.size()==3;
            result["manual_retry_and_latest"]=check_metadata({"/meta/current","/meta/fail","/meta/fail"})&&fixture.phase_==Phase::Latest&&fixture.error_.isEmpty();
            requests.clear();fixture.check();QCoreApplication::processEvents();result["successful_checks_are_throttled"]=requests.isEmpty()&&fixture.phase_==Phase::Latest;
            fixture.fixture_metadata_={base+"/meta/hang",base+"/meta/valid",base+"/meta/api"};fixture.check(true);fixture.cancel();
            QElapsedTimer quiet;quiet.start();wait([&]{return quiet.elapsed()>200;});result["cancelled_metadata_does_not_restart"]=fixture.phase_==Phase::Idle&&!fixture.reply_;
        }
        bool progress=false;if(listening){configure("setup.exe",{"/bad","/good"});bool finished=wait([&]{progress=progress||(fixture.received_>0&&fixture.received_<fixture.total_);return !fixture.busy()||fixture.phase_==Phase::Preparing;});result["checksum_fallback"]=finished&&fixture.phase_==Phase::Preparing&&requests.contains("/bad")&&requests.contains("/good")&&bytes(fixture.setup_)==payload;result["download_progress"]=progress;
            configure("cancel.exe",{"/good"});bool began=wait([&]{return fixture.received_>0||!fixture.busy();});fixture.cancel();result["cancel_no_partial_installer"]=began&&fixture.phase_==Phase::Available&&!QFile::exists(directory+"/cancel.exe");
            configure("oversize.exe",{"/oversize"});bool oversize=wait([&]{return !fixture.busy();});result["oversize_rejected"]=oversize&&fixture.phase_==Phase::Error&&!QFile::exists(fixture.setup_);
            QString prior=directory+"/preserved.exe";{QFile f(prior);f.open(QIODevice::WriteOnly);f.write("existing verified file");}configure("preserved.exe",{"/html"});bool html=wait([&]{return !fixture.busy();});result["failed_download_preserves_existing"]=html&&fixture.phase_==Phase::Error&&bytes(prior)=="existing verified file";
            configure("unknown.exe",{"/unknown"});fixture.release_.size=0;
            result["unknown_size_download_still_verifies_hash"]=wait([&]{return !fixture.busy()||fixture.phase_==Phase::Preparing;})&&fixture.phase_==Phase::Preparing&&bytes(fixture.setup_)==payload;
        }
        fixture.cancel();server.close();bool ok=true;for(auto it=result.begin();it!=result.end();++it)ok=it.value().toBool()&&ok;result["passed"]=ok;result["requests"]=QJsonArray::fromStringList(requests);result["external_network_requests"]=0;QSaveFile report(directory+"/update-ui-checks.json");if(!report.open(QIODevice::WriteOnly))return false;auto data=QJsonDocument(result).toJson();return report.write(data)==data.size()&&report.commit()&&ok;
    }
};
inline std::unique_ptr<UpdateService> update_service;
inline UpdateService& updates(){if(!update_service)update_service=std::make_unique<UpdateService>();return *update_service;}
inline QPushButton* update_button(QWidget* owner){auto b=new QPushButton(owner);auto refresh=[b]{b->setText(updates().text());b->setToolTip(updates().tooltip());b->setEnabled(updates().enabled());};QObject::connect(b,&QPushButton::clicked,b,[]{updates().click();});auto timer=new QTimer(b);timer->setInterval(200);QObject::connect(timer,&QTimer::timeout,b,refresh);timer->start();refresh();return b;}
}
