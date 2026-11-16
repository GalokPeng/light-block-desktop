#include "ui.hpp"
#include "dialogs.hpp"
#include "../src/window_shape.hpp"
#include <map>
#include <QStringConverter>
namespace ui {
namespace {
QString metadata(QString const& file) { return file+".ini"; }
constexpr qint64 note_limit=1024*1024;
QString read(QString const& file,bool* success=nullptr) {
    if(success) *success=false;QFile input(file);if(!input.open(QIODevice::ReadOnly)||input.size()>note_limit) return {};
    auto bytes=input.read(note_limit+1);if(bytes.size()>note_limit||input.error()!=QFile::NoError) return {};QStringDecoder decode(QStringDecoder::Utf8);QString text=decode(bytes);if(decode.hasError()) return {};if(success) *success=true;return text;
}
std::map<QString,QPointer<QTextDocument>> documents;
QTextDocument* document(QString const& file) { auto& value=documents[file];if(!value) { value=new QTextDocument(qApp);value->setDocumentLayout(new QPlainTextDocumentLayout(value));value->setDocumentMargin(0);value->setPlainText(read(file));value->setModified(false); }return value; }
bool write(QString const& file,QString const& body) { auto bytes=body.toUtf8();if(bytes.size()>note_limit||QFileInfo(file).size()>note_limit) return false;QSaveFile output(file);return output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit(); }
QString save_error() { return ui::tr("保存失败或内容超过 1 MiB。文字仍保留在编辑区，请缩短内容或检查目录权限。","Save failed or text exceeds 1 MiB. Your text remains in the editor; shorten it or check folder permissions."); }
QColor note_color(QString const& file) { QSettings meta(metadata(file),QSettings::IniFormat);return color(meta.value("note/color",rgb(QColor("#a6e4ca"))).toInt()); }
class Note;
std::map<QString,Note*> pins;
bool preserving{};
class Note:public QWidget {
    QString file_;QPlainTextEdit *body_{};QTimer save_;QPoint drag_;bool dragging_{},loading_{true},interactive_{};QColor tint_;QPushButton* palette_{};QPushButton* close_{};
    QString title() const { QSettings meta(metadata(file_),QSettings::IniFormat);auto name=meta.value("note/title").toString();if(name.isEmpty()) name=body_->toPlainText().section('\n',0,0).trimmed();return name.isEmpty()?ui::tr("未命名便签","Untitled note"):name; }
    void scheme() { body_->setStyleSheet("QPlainTextEdit {background:transparent;color:#edf3fa;border:0;font:10pt 'Microsoft YaHei UI';selection-background-color:#476c7b;}");update(); }
public:
    Note(QString file):QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint),file_(file),tint_(note_color(file)) {
        setWindowTitle(ui::tr("轻块桌面 · 桌面便签","Light Block · Desktop note"));setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_ShowWithoutActivating);setMinimumSize(220,150);setMouseTracking(true);body_=new QPlainTextEdit(this);body_->setFrameShape(QFrame::NoFrame);body_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);body_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);body_->document()->setDocumentMargin(0);body_->setDocument(document(file_));body_->setMouseTracking(true);palette_=button("◉",[this]{QPointer<Note> self(this);auto chosen=ui::dialogs::color(tint_,this,ui::tr("便签颜色","Note color"));if(self&&chosen.isValid()) { tint_=chosen;save();scheme();notes_refresh_page(); }},this);close_=button("×",[this]{close();},this);for(auto b:{palette_,close_}) b->setStyleSheet("QPushButton {background:transparent;color:#d9ece6;border:0;border-radius:6px;padding:0;font-size:17px;} QPushButton:hover {background:#426158;}");save_.setSingleShot(true);save_.setInterval(300);connect(&save_,&QTimer::timeout,this,[this]{save();});connect(body_,&QPlainTextEdit::textChanged,this,[this]{if(!loading_) { save_.start();update(); }});QSettings meta(metadata(file_),QSettings::IniFormat);resize(std::clamp(meta.value("note/width",290).toInt(),220,1400),std::clamp(meta.value("note/height",240).toInt(),150,1400));move(meta.value("note/x",180).toInt(),meta.value("note/y",160).toInt());auto screen=QGuiApplication::screenAt(geometry().center());if(!screen) screen=QGuiApplication::primaryScreen();if(screen) { auto area=screen->availableGeometry();resize(std::min(width(),std::max(minimumWidth(),area.width())),std::min(height(),std::max(minimumHeight(),area.height())));move(std::clamp(x(),area.left(),std::max(area.left(),area.right()-width()+1)),std::clamp(y(),area.top(),std::max(area.top(),area.bottom()-height()+1))); }scheme();setToolTip(ui::tr("拖动标题移动 · 拖动边缘缩放 · 滚轮浏览 · 右键管理","Drag title to move · Drag edges to resize · Scroll content · Right click to manage"));auto hwnd=reinterpret_cast<HWND>(winId());SetWindowLongPtrW(hwnd,GWL_STYLE,GetWindowLongPtrW(hwnd,GWL_STYLE)|WS_THICKFRAME);SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);lb::window_shape::own_frame(hwnd);loading_=false;
    }
    void activate_body() {
        interactive_=true;auto hwnd=reinterpret_cast<HWND>(winId());SetWindowLongPtrW(hwnd,GWLP_HWNDPARENT,0);SetWindowPos(hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(hwnd);activateWindow();body_->setFocus(Qt::OtherFocusReason);
    }
    bool event(QEvent* event) override {
        if(event->type()==QEvent::WindowDeactivate&&interactive_) QTimer::singleShot(0,this,[this]{auto modal=QApplication::activeModalWidget(),popup=QApplication::activePopupWidget();if((modal&&(modal==this||isAncestorOf(modal)))||(popup&&(popup==this||isAncestorOf(popup)))||isActiveWindow()) return;interactive_=false;desktop_layer(reinterpret_cast<HWND>(winId()));});
        return QWidget::event(event);
    }
    void enterEvent(QEnterEvent*) override { setWindowOpacity(1); }
    void leaveEvent(QEvent*) override { setWindowOpacity(.72); }
    bool save() { if(loading_) return true;save_.stop();if(!write(file_,body_->toPlainText())) { setToolTip(save_error());return false; }body_->document()->setModified(false);QSettings meta(metadata(file_),QSettings::IniFormat);meta.setValue("note/color",rgb(tint_));meta.setValue("note/x",x());meta.setValue("note/y",y());meta.setValue("note/width",width());meta.setValue("note/height",height());meta.setValue("note/pinned",1);meta.sync();return meta.status()==QSettings::NoError; }
    void reload() { bool valid=false;auto contents=read(file_,&valid);if(valid&&contents!=body_->toPlainText()&&!body_->document()->isModified()) { loading_=true;auto scroll=body_->verticalScrollBar()->value();body_->setPlainText(contents);body_->verticalScrollBar()->setValue(scroll);loading_=false; }tint_=note_color(file_);scheme(); }
    void resizeEvent(QResizeEvent*) override { body_->setGeometry(18,64,std::max(1,width()-36),std::max(1,height()-83));palette_->setGeometry(width()-68,13,26,28);close_->setGeometry(width()-36,13,26,28);if(!loading_) save_.start(); }
    void moveEvent(QMoveEvent*) override { if(!loading_) save_.start(); }
    void showEvent(QShowEvent*) override { setWindowOpacity(geometry().contains(QCursor::pos())?1:.72);QTimer::singleShot(0,this,[this]{if(!interactive_) desktop_layer(reinterpret_cast<HWND>(winId()));}); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto r=QRectF(rect()).adjusted(.5,.5,-.5,-.5);auto shade=[this](int red,int green,int blue,int alpha){return QColor(qRound(red*.67+tint_.red()*.33),qRound(green*.67+tint_.green()*.33),qRound(blue*.67+tint_.blue()*.33),alpha);};QLinearGradient fill(0,0,0,height());fill.setColorAt(0,shade(39,51,71,217));fill.setColorAt(1,shade(20,28,43,237));p.setBrush(fill);auto edge=tint_;edge.setAlpha(125);p.setPen(QPen(edge,1));p.drawRoundedRect(r,14,14);p.setPen(Qt::NoPen);p.setBrush(tint_);p.drawEllipse(QPoint(23,29),4,4);p.setPen(QColor("#f4f7fc"));QFont font("Microsoft YaHei UI",12,QFont::DemiBold);p.setFont(font);p.drawText(QRect(34,12,width()-110,34),Qt::AlignVCenter,QFontMetrics(font).elidedText(title(),Qt::ElideRight,width()-110));p.setPen(QColor(210,226,244,48));p.drawLine(18,52,width()-18,52);p.setPen(QColor(232,241,250,100));for(int offset:{0,5}) p.drawLine(width()-20+offset,height()-8,width()-8,height()-20+offset);
    }
    bool nativeEvent(QByteArray const&,void* event,qintptr* result) override { auto msg=static_cast<MSG*>(event);if(msg->message==WM_NCCALCSIZE) { *result=0;return true; }if(msg->message==WM_NCHITTEST) { RECT r{};GetWindowRect(msg->hwnd,&r);int x=short(LOWORD(msg->lParam)),y=short(HIWORD(msg->lParam));int grip=qRound(8*devicePixelRatioF());bool left=x<r.left+grip,right=x>=r.right-grip,top=y<r.top+grip,bottom=y>=r.bottom-grip;int hit=top&&left?HTTOPLEFT:top&&right?HTTOPRIGHT:bottom&&left?HTBOTTOMLEFT:bottom&&right?HTBOTTOMRIGHT:left?HTLEFT:right?HTRIGHT:top?HTTOP:bottom?HTBOTTOM:0;if(hit) { *result=hit;return true; } }return false; }
    void mousePressEvent(QMouseEvent* e) override { if(e->button()==Qt::LeftButton&&e->position().y()<54) { drag_=e->globalPosition().toPoint()-pos();dragging_=true; } }
    void mouseMoveEvent(QMouseEvent* e) override { if(dragging_) move(e->globalPosition().toPoint()-drag_); }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_=false;save_.start(); }
    void contextMenuEvent(QContextMenuEvent* e) override { QPointer<Note> self(this);QMenu menu;auto manage=menu.addAction(ui::tr("管理便签与提醒","Manage notes and reminders"));auto done=menu.addAction(ui::tr("切换完成状态","Toggle completed"));auto archive=menu.addAction(ui::tr("归档","Archive"));auto chosen=menu.exec(e->globalPos());if(!self)return;if(chosen==manage) { if(save()) api.action(705); }else if(chosen==done||chosen==archive) { QSettings meta(metadata(file_),QSettings::IniFormat);meta.setValue(chosen==done?"note/done":"note/archived",chosen==done?int(!meta.value("note/done",false).toBool()):1);meta.sync();api.notes_changed();notes_refresh_page();if(chosen==archive) close(); } }
    QJsonObject qa(QString const& directory) {
        QJsonObject result;resize(420,300);resizeEvent(nullptr);ensurePolished();
        RECT bounds{};GetWindowRect(reinterpret_cast<HWND>(winId()),&bounds);LONG cx=(bounds.left+bounds.right)/2,cy=(bounds.top+bounds.bottom)/2;
        struct Hit { LONG x,y;int expected; };Hit zones[]={{bounds.left+2,bounds.top+2,HTTOPLEFT},{cx,bounds.top+2,HTTOP},{bounds.right-2,bounds.top+2,HTTOPRIGHT},{bounds.right-2,cy,HTRIGHT},{bounds.right-2,bounds.bottom-2,HTBOTTOMRIGHT},{cx,bounds.bottom-2,HTBOTTOM},{bounds.left+2,bounds.bottom-2,HTBOTTOMLEFT},{bounds.left+2,cy,HTLEFT}};
        bool hit=true;for(auto const& zone:zones) { MSG message{};message.hwnd=reinterpret_cast<HWND>(winId());message.message=WM_NCHITTEST;message.lParam=MAKELPARAM(zone.x,zone.y);qintptr actual{};hit=hit&&nativeEvent({},&message,&actual)&&actual==zone.expected; }result["eight_resize_edges"]=hit;
        QString content;for(int n=0;n<120;++n) content+=QString::fromUtf8("第 %1 行 · C++ 桌面便签与滚动测试\n").arg(n+1);body_->setPlainText(content);body_->moveCursor(QTextCursor::End);qApp->processEvents();auto scroll=body_->verticalScrollBar();scroll->setValue(scroll->maximum());result["hidden_scrollbar_long_content"]=scroll->maximum()>0&&scroll->value()==scroll->maximum()&&body_->verticalScrollBarPolicy()==Qt::ScrollBarAlwaysOff;
        QPlainTextEdit second;second.setDocument(document(file_));QTextCursor cursor(second.document());cursor.movePosition(QTextCursor::End);cursor.insertText(QString::fromUtf8("同步编辑 · shared document"));bool shared=body_->toPlainText()==second.toPlainText();result["shared_document"]=shared;
        bool stored=save()&&read(file_)==body_->toPlainText();result["unicode_body_roundtrip"]=stored;QSettings meta(metadata(file_),QSettings::IniFormat);result["resize_persistence"]=meta.value("note/width").toInt()==420&&meta.value("note/height").toInt()==300;
        QImage frame(size(),QImage::Format_ARGB32);frame.fill(Qt::transparent);render(&frame);result["rendered"]=frame.save(directory+"/cpp-desktop-note.png");result["transparent_corner_pixels"]=qAlpha(frame.pixel(0,0))==0&&qAlpha(frame.pixel(frame.width()-1,0))==0&&qAlpha(frame.pixel(0,frame.height()-1))==0&&qAlpha(frame.pixel(frame.width()-1,frame.height()-1))==0;save_.stop();return result;
    }
    void closeEvent(QCloseEvent* e) override { if(!save()) { e->ignore();return; }QSettings meta(metadata(file_),QSettings::IniFormat);if(!preserving) meta.setValue("note/pinned",0);meta.sync();if(meta.status()!=QSettings::NoError) { setToolTip(ui::tr("保存失败，便签仍保留在桌面。","Save failed. The note remains on your desktop."));e->ignore();return; }pins.erase(file_);setAttribute(Qt::WA_DeleteOnClose);e->accept();notes_refresh_page(); }
};
class NotesRoot final:public QWidget {
    QListWidget* list_{};QPlainTextEdit* body_{};int theme_{-1};
    void refresh_theme(){if(!list_||!body_||theme_==state.dark)return;theme_=state.dark;
        list_->setStyleSheet(state.dark?"QListWidget {background:#192330;border:0;border-radius:10px;outline:0;} QListWidget::item {padding:12px;} QListWidget::item:selected {background:#345347;}":"QListWidget {background:#edf2f2;border:0;border-radius:10px;outline:0;} QListWidget::item {padding:12px;} QListWidget::item:selected {background:#cce8db;}");
        body_->setStyleSheet(state.dark?"QPlainTextEdit {background:#18222f;border:1px solid #344253;border-radius:10px;padding:10px;}":"QPlainTextEdit {background:#fbfdfc;border:1px solid #c9d7d0;border-radius:10px;padding:10px;}");
    }
public:
    explicit NotesRoot(QWidget* parent):QWidget(parent){}
    void attach(QListWidget* list,QPlainTextEdit* body){list_=list;body_=body;refresh_theme();}
protected:
    bool event(QEvent* event)override{bool handled=QWidget::event(event);if(event->type()==QEvent::PaletteChange||event->type()==QEvent::ApplicationPaletteChange)refresh_theme();return handled;}
};
struct Page {
    QPointer<QWidget> window;QListWidget* list{};QLineEdit *query{},*title{};QPlainTextEdit* body{};
    QComboBox *filter{},*colors{};QCheckBox *done{},*pinned{},*has_due{};QDateTimeEdit* due{};
    QPushButton *palette{},*archive{};QLabel* status{};QTimer* save{};QString selected;bool loading{},dirty{},fixture{};
} page;
QString fixture_notes_root;
QStringList files() { QDir dir(fixture_notes_root.isEmpty()?s(state.data)+"/notes":fixture_notes_root);return dir.entryList({"note-*.txt"},QDir::Files,QDir::Time).replaceInStrings(QRegularExpression("^"),dir.absolutePath()+"/"); }
QIcon note_swatch(QColor color) {QPixmap image(32,32);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QColor("#83969e"));p.setBrush(color);p.drawRoundedRect(QRectF(2,2,28,28),6,6);p.end();image.setDevicePixelRatio(2);return QIcon(image);}
void select_note_color(int value) {
    QSignalBlocker block(page.colors);int index=page.colors->findData(value);
    if(index<0){index=4;if(page.colors->count()==4)page.colors->addItem({},value);page.colors->setItemText(index,ui::tr("自定义","Custom"));page.colors->setItemData(index,value);page.colors->setItemIcon(index,note_swatch(color(value)));}
    page.colors->setCurrentIndex(index);auto name=color(value).name().toUpper();page.colors->setToolTip(name);page.colors->setItemData(index,name,Qt::ToolTipRole);page.palette->setToolTip(name);page.palette->setIcon(note_swatch(color(value)));
}
void refresh_options() {
    bool selected=!page.selected.isEmpty();page.loading=true;
    for(auto control:std::initializer_list<QWidget*>{page.title,page.body,page.colors,page.done,page.pinned,page.has_due,page.palette,page.archive})control->setEnabled(selected);
    std::unique_ptr<QSettings> meta;if(selected)meta=std::make_unique<QSettings>(metadata(page.selected),QSettings::IniFormat);
    page.done->setChecked(selected&&meta->value("note/done",0).toBool());page.pinned->setChecked(selected&&meta->value("note/pinned",0).toBool());
    auto stamp=selected?meta->value("note/due",0).toULongLong():0;page.has_due->setChecked(stamp>116444736000000000ULL);page.due->setEnabled(selected&&page.has_due->isChecked());
    page.due->setDateTime(stamp>116444736000000000ULL?QDateTime::fromMSecsSinceEpoch(qint64((stamp-116444736000000000ULL)/10000)):QDateTime::currentDateTime().addSecs(3600));
    select_note_color(selected?meta->value("note/color",rgb(QColor("#a6e4ca"))).toInt():rgb(QColor("#a6e4ca")));
    page.archive->setText(selected&&meta->value("note/archived",0).toBool()?ui::tr("还原便签","Restore note"):ui::tr("归档便签","Archive note"));page.loading=false;
}
bool save_page() {
    if(!page.window||page.loading||page.selected.isEmpty()) return true;
    page.save->stop();
    if(!page.dirty&&!page.body->document()->isModified())return true;
    if(!write(page.selected,page.body->toPlainText())) { page.status->setText(save_error());return false; }
    QSettings meta(metadata(page.selected),QSettings::IniFormat);meta.setValue("note/title",page.title->text());meta.sync();
    if(meta.status()!=QSettings::NoError) { page.status->setText(ui::tr("标题保存失败，请保留编辑区后重试。","Title could not be saved. Keep this editor open and retry."));return false; }
    page.body->document()->setModified(false);page.dirty=false;if(auto pin=pins.find(page.selected);pin!=pins.end()) pin->second->reload();return true;
}
void list_notes();
bool set_metadata(QString const& key,QVariant value,bool notify=true) {
    if(page.selected.isEmpty()||!save_page())return false;
    QSettings meta(metadata(page.selected),QSettings::IniFormat);meta.setValue(key,value);meta.sync();
    if(meta.status()!=QSettings::NoError){page.status->setText(ui::tr("设置保存失败，便签内容仍保留。","Settings could not be saved. Your note text is preserved."));refresh_options();return false;}
    if(auto pin=pins.find(page.selected);pin!=pins.end())pin->second->reload();
    if(notify&&!page.fixture)api.notes_changed();return true;
}
bool set_pinned(bool enabled) {
    if(page.loading||page.selected.isEmpty()||!save_page())return false;auto path=page.selected;
    if(page.fixture)return set_metadata("note/pinned",int(enabled),false);
    if(enabled){if(!set_metadata("note/pinned",1,false))return false;note_open(w(path),0);if(!pins.contains(path)){set_metadata("note/pinned",0,false);refresh_options();return false;}}
    else {if(pins.contains(path)){note_close(w(path));if(pins.contains(path)){page.status->setText(save_error());refresh_options();return false;}}if(!set_metadata("note/pinned",0,false))return false;}
    api.notes_changed();refresh_options();return true;
}
void save_due() {
    if(page.loading||page.selected.isEmpty())return;qulonglong stamp=page.has_due->isChecked()?qulonglong(page.due->dateTime().toMSecsSinceEpoch())*10000+116444736000000000ULL:0;
    if(set_metadata("note/due",QString::number(stamp),false)){set_metadata("note/notified",0);page.due->setEnabled(page.has_due->isChecked());}
}
void select_item() {
    auto item=page.list->currentItem();if(!item) return;auto next=item->data(Qt::UserRole).toString();if(next==page.selected){refresh_options();return;}
    bool valid=false;auto content=read(next,&valid);
    if(!valid||!save_page()) { QSignalBlocker block(page.list);page.list->setCurrentRow(-1);for(int n=0;n<page.list->count();++n) if(page.list->item(n)->data(Qt::UserRole).toString()==page.selected) { page.list->setCurrentRow(n);break; }if(!valid) page.status->setText(ui::tr("便签超过 1 MiB 或无法读取，原文件已保留。","The note exceeds 1 MiB or cannot be read. The original file is preserved."));return; }
    page.loading=true;page.selected=next;page.dirty=false;QSettings meta(metadata(next),QSettings::IniFormat);page.title->setText(meta.value("note/title",content.section('\n',0,0)).toString());page.body->setDocument(document(next));page.loading=false;refresh_options();
}
bool matches_note_filter(bool archived,bool done,int filter) {return archived==(filter==-2)&&(filter<0||done==(filter==1));}
void list_notes() {
    if(!page.window||!save_page()) return;auto selected=page.selected;QSignalBlocker block(page.list);page.list->clear();
    int filter=page.filter->currentData().toInt();
    for(auto const& file:files()) {QSettings meta(metadata(file),QSettings::IniFormat);bool done=meta.value("note/done",0).toBool();if(!matches_note_filter(meta.value("note/archived",0).toBool(),done,filter))continue;auto content=read(file);auto title=meta.value("note/title",content.section('\n',0,0)).toString().trimmed();if(title.isEmpty())title=ui::tr("未命名便签","Untitled note");if(!page.query->text().isEmpty()&&!title.contains(page.query->text(),Qt::CaseInsensitive)&&!content.contains(page.query->text(),Qt::CaseInsensitive))continue;auto summary=content.simplified().left(55);auto item=new QListWidgetItem((done?ui::tr("已完成 · ","Done · "):QString{})+title+"\n"+summary,page.list);item->setData(Qt::UserRole,file);item->setToolTip(content.left(300));item->setIcon(note_swatch(note_color(file)));if(file==selected)page.list->setCurrentItem(item);}
    if(!page.list->currentItem()) {page.loading=true;page.selected.clear();page.dirty=false;page.title->clear();auto empty=new QTextDocument(page.body);empty->setDocumentLayout(new QPlainTextDocumentLayout(empty));page.body->setDocument(empty);page.loading=false;if(page.list->count()){page.list->setCurrentRow(0);select_item();}else refresh_options();}
    else refresh_options();
}
}
void desktop_layer(HWND window) { HWND owner=nullptr;auto callback=+[](HWND candidate,LPARAM data)->BOOL { if(FindWindowExW(candidate,nullptr,L"SHELLDLL_DefView",nullptr)) { *reinterpret_cast<HWND*>(data)=candidate;return FALSE; }return TRUE; };EnumWindows(callback,LPARAM(&owner));if(!owner) owner=FindWindowW(L"Progman",nullptr);if(owner) SetWindowLongPtrW(window,GWLP_HWNDPARENT,LONG_PTR(owner));SetWindowPos(window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); }
void note_open(wchar_t const* file,int activate) { QString path=s(file);if(auto found=pins.find(path);found!=pins.end()) { found->second->reload();found->second->show();if(activate) { found->second->raise();found->second->activateWindow();found->second->activate_body(); }return; }bool readable=false;read(path,&readable);if(!readable) { QMessageBox::warning(nullptr,ui::tr("无法打开便签","Cannot open note"),ui::tr("便签超过 1 MiB、编码无效或无法读取。原文件保持不变。","The note exceeds 1 MiB, has invalid encoding, or cannot be read. The original file is unchanged."));return; }auto note=new Note(path);pins[path]=note;QSettings meta(metadata(path),QSettings::IniFormat);meta.setValue("note/pinned",1);meta.sync();note->show();if(activate) { note->activateWindow();note->activate_body(); }notes_refresh_page(); }
void note_close(wchar_t const* file) { auto found=pins.find(s(file));if(found!=pins.end()) found->second->close(); }
int notes_shutdown(int preserve) { if(!save_page()) return 0;preserving=preserve!=0;auto copy=pins;for(auto const& pin:copy) pin.second->close();preserving=false;return pins.empty(); }
void refresh_notes() { for(auto const& pin:pins) pin.second->reload();notes_refresh_page(); }
void notes_refresh_page() { if(page.window) list_notes(); }
bool notes_flush() { return save_page(); }
bool notes_flush_all() { bool ok=save_page();for(auto const& pin:pins) ok=pin.second->save()&&ok;return ok; }
bool notes_ui_tests(QString const& directory) {
    if(!QDir().mkpath(directory)) return false;
    // Test the real controls against isolated files; never pin windows or notify the engine.
    struct PageRestore {
        Page previous;QString previous_root;bool timer_active{};
        ~PageRestore(){if(page.window&&page.window!=previous.window)delete page.window.data();page=previous;fixture_notes_root=previous_root;if(timer_active&&page.window)page.save->start();}
    } restore{page,fixture_notes_root,page.window&&page.save->isActive()};
    if(page.window)page.save->stop();page=Page{};fixture_notes_root=QDir(directory).absolutePath()+"/manager";QDir().mkpath(fixture_notes_root);
    QString fixture=directory+"/note-ui-fixture.txt";if(!write(fixture,QString::fromUtf8("便签测试"))) return false;
    Note note(fixture);auto report=note.qa(directory);
    // A metadata write failure must leave the editor alive with its text.
    QString blocked=directory+"/note-ui-blocked.txt";write(blocked,QString::fromUtf8("请保留这条内容"));QDir().mkpath(metadata(blocked));Note unsaved(blocked);QCloseEvent close;unsaved.closeEvent(&close);report["failed_metadata_save_retains_note"]=!close.isAccepted();
    QString boundary=directory+"/note-ui-boundary.txt";bool exact=write(boundary,QString(note_limit,QChar('a'))),readable=false;auto loaded=read(boundary,&readable);bool rejected=!write(boundary,QString(note_limit+1,QChar('a')))&&!write(boundary,QString(note_limit/3+1,QChar(0x4e2d)));report["utf8_byte_limit_keeps_prior_file"]=exact&&readable&&loaded.size()==note_limit&&rejected&&QFileInfo(boundary).size()==note_limit;
    QString oversized=directory+"/note-ui-oversized.txt";QFile large(oversized);bool created=large.open(QIODevice::WriteOnly)&&large.write(QByteArray(note_limit+1,'x'))==note_limit+1;large.close();read(oversized,&readable);report["oversized_existing_file_not_truncated"]=created&&!readable&&!write(oversized,"replacement")&&QFileInfo(oversized).size()==note_limit+1;
    auto active=fixture_notes_root+"/note-active.txt",completed=fixture_notes_root+"/note-completed.txt",archived=fixture_notes_root+"/note-archived.txt";
    bool fixtures=true;for(auto const& path:{active,completed,archived}) {
        fixtures=write(path,path==active?QString::fromUtf8("独特正文 · 下一步行动\n在这里记录一个想法。"):QString::fromUtf8("演示内容"))&&fixtures;
        QSettings meta(metadata(path),QSettings::IniFormat);meta.clear();meta.setValue("note/title",path==active?ui::tr("设计下一步","Design the next step"):path==completed?ui::tr("已完成的想法","Finished idea"):ui::tr("归档的记录","Archived record"));meta.setValue("note/done",path==completed?1:0);meta.setValue("note/archived",path==archived?1:0);meta.setValue("note/color",rgb(QColor("#a6e4ca")));meta.sync();fixtures=fixtures&&meta.status()==QSettings::NoError;
    }
    auto manager=notes_page();page.fixture=true;manager->resize(1060,610);QPalette test_palette=manager->palette();test_palette.setColor(QPalette::Window,state.dark?QColor("#11151c"):QColor("#f4f7f5"));manager->setPalette(test_palette);manager->ensurePolished();manager->layout()->activate();
    auto select_path=[](QString const& path){for(int n=0;n<page.list->count();++n)if(page.list->item(n)->data(Qt::UserRole).toString()==path){page.list->setCurrentRow(n);return page.selected==path;}return false;};
    bool filters=page.list->count()==2;
    page.filter->setCurrentIndex(1);filters=filters&&page.list->count()==1&&page.selected==active;
    page.filter->setCurrentIndex(2);filters=filters&&page.list->count()==1&&page.selected==completed;
    page.filter->setCurrentIndex(3);filters=filters&&page.list->count()==1&&page.selected==archived;
    page.filter->setCurrentIndex(0);report["manager_original_filter_semantics"]=fixtures&&filters&&!page.filter->isEditable();
    page.query->setText(QString::fromUtf8("独特正文"));report["manager_fulltext_search"]=page.list->count()==1&&page.selected==active;page.query->clear();select_path(active);
    bool presets=true;for(int n=0;n<4;++n){page.colors->setCurrentIndex(n);QSettings meta(metadata(active),QSettings::IniFormat);presets=presets&&meta.value("note/color",rgb(QColor("#a6e4ca"))).toInt()==page.colors->itemData(n).toInt();}
    int custom=rgb(QColor("#b58bea"));bool saved_color=set_metadata("note/color",custom,false);select_note_color(custom);list_notes();
    report["manager_preset_and_custom_colors"]=presets&&saved_color&&note_color(active)==color(custom)&&page.colors->currentData().toInt()==custom&&page.colors->currentText()==ui::tr("自定义","Custom")&&page.colors->minimumContentsLength()>=9&&!page.colors->isEditable();
    auto beforeActiveColor=note_color(active),beforeCompletedColor=note_color(completed);auto previousPicker=dialogs::color_fixture;dialogs::color_fixture=[&](QColor const&,QWidget*,QString const&){select_path(completed);return QColor("#ff1234");};page.palette->click();dialogs::color_fixture=previousPicker;report["palette_selection_change_does_not_write_wrong_note"]=page.selected==completed&&note_color(active)==beforeActiveColor&&note_color(completed)==beforeCompletedColor;select_path(active);
    page.done->setChecked(true);{QSettings meta(metadata(active),QSettings::IniFormat);report["manager_completed_toggle"]=meta.value("note/done").toBool();}page.done->setChecked(false);
    auto initial_pins=pins.size();page.pinned->setChecked(true);bool pinned_value=false;{QSettings meta(metadata(active),QSettings::IniFormat);pinned_value=meta.value("note/pinned").toBool();}page.pinned->setChecked(false);{QSettings meta(metadata(active),QSettings::IniFormat);report["manager_pin_unpin_without_desktop_side_effects"]=pinned_value&&!meta.value("note/pinned").toBool()&&pins.size()==initial_pins;}
    page.title->setText(QString::fromUtf8("自动保存 · 标题"));page.body->setPlainText(QString::fromUtf8("新的内容不会丢失。\nShared document."));bool saved_editor=save_page();{QSettings meta(metadata(active),QSettings::IniFormat);report["manager_editor_atomic_save"]=saved_editor&&read(active)==page.body->toPlainText()&&meta.value("note/title").toString()==page.title->text();}
    page.has_due->setChecked(true);auto chosen_time=QDateTime::currentDateTime().addSecs(7200);page.due->setDateTime(chosen_time);bool due_value=false;{QSettings meta(metadata(active),QSettings::IniFormat);auto stamp=meta.value("note/due").toULongLong();due_value=stamp>116444736000000000ULL&&qAbs(qint64((stamp-116444736000000000ULL)/10000)-chosen_time.toMSecsSinceEpoch())<1000;}page.has_due->setChecked(false);{QSettings meta(metadata(active),QSettings::IniFormat);report["manager_reminder_immediate_save"]=due_value&&meta.value("note/due").toULongLong()==0&&!page.due->isEnabled();}
    page.pinned->setChecked(true);page.archive->click();bool archived_value=false;{QSettings meta(metadata(active),QSettings::IniFormat);archived_value=meta.value("note/archived").toBool()&&!meta.value("note/pinned").toBool();}page.filter->setCurrentIndex(3);select_path(active);page.archive->click();{QSettings meta(metadata(active),QSettings::IniFormat);report["manager_archive_restore"]=archived_value&&!meta.value("note/archived").toBool();}page.filter->setCurrentIndex(0);select_path(active);
    QImage manager_frame(manager->size(),QImage::Format_ARGB32);manager_frame.fill(state.dark?QColor("#11151c"):QColor("#f4f7f5"));manager->render(&manager_frame);report["manager_rendered"]=manager_frame.save(directory+"/cpp-notes-manager.png");
    auto previous_theme=state.dark;state.dark=!previous_theme;QEvent palette_event(QEvent::ApplicationPaletteChange);QApplication::sendEvent(manager,&palette_event);report["manager_live_theme_updates"]=page.list->styleSheet().contains(state.dark?"#192330":"#edf2f2")&&page.body->styleSheet().contains(state.dark?"#18222f":"#fbfdfc");state.dark=previous_theme;QApplication::sendEvent(manager,&palette_event);
    report["manager_fixture_does_not_create_desktop_windows"]=pins.size()==initial_pins;
    QPointer<QWidget> guardedManager(manager);dialogs::color_fixture=[](QColor const&,QWidget* owner,QString const&){delete owner;return QColor("#ff1234");};page.palette->click();dialogs::color_fixture=previousPicker;report["palette_owner_destroyed_during_dialog"]=guardedManager.isNull();manager=nullptr;
    bool ok=true;for(auto value:report) ok=ok&&value.toBool();report["passed"]=ok;QSaveFile output(directory+"/notes-ui-checks.json");if(!output.open(QIODevice::WriteOnly)) return false;auto bytes=QJsonDocument(report).toJson();return output.write(bytes)==bytes.size()&&output.commit()&&ok;
}
QWidget* notes_page(QWidget* parent) {
    if(page.window) { notes_refresh_page();return page.window; }
    page=Page{};auto root=new NotesRoot(parent);page.window=root;root->setObjectName("notesManager");
    auto outer=new QVBoxLayout(root);outer->setContentsMargins(0,0,0,0);outer->setSpacing(16);
    outer->addWidget(label(ui::tr("留住想法，也完成它。","Keep your ideas. Get things done."),"section"));
    auto top=new QHBoxLayout;top->setSpacing(10);
    page.query=new QLineEdit;page.query->setPlaceholderText(ui::tr("搜索标题或正文…","Search titles or notes…"));top->addWidget(page.query,1);
    page.filter=new QComboBox;page.filter->setEditable(false);
    page.filter->addItem(ui::tr("全部","All"),-1);page.filter->addItem(ui::tr("待办","Active"),0);page.filter->addItem(ui::tr("已完成","Completed"),1);page.filter->addItem(ui::tr("归档","Archived"),-2);top->addWidget(page.filter);
    top->addWidget(button(ui::tr("＋ 新建便签","＋ New note"),[]{
        if(!save_page())return;
        auto directory=fixture_notes_root.isEmpty()?s(state.data)+"/notes":fixture_notes_root;
        auto path=directory+"/note-"+QUuid::createUuid().toString(QUuid::Id128)+".txt";
        if(!QDir().mkpath(directory)||!write(path,{})){page.status->setText(save_error());return;}
        QSettings meta(metadata(path),QSettings::IniFormat);meta.setValue("note/title",ui::tr("未命名便签","Untitled note"));meta.setValue("note/color",rgb(QColor("#a6e4ca")));meta.sync();
        if(meta.status()!=QSettings::NoError){page.status->setText(save_error());return;}
        {QSignalBlocker a(page.query),b(page.filter);page.query->clear();page.filter->setCurrentIndex(0);}list_notes();
        for(int n=0;n<page.list->count();++n)if(page.list->item(n)->data(Qt::UserRole).toString()==path){page.list->setCurrentRow(n);break;}
        if(!page.fixture)api.notes_changed();page.body->setFocus();
    },nullptr,"primary"));
    page.archive=button(ui::tr("归档便签","Archive note"),[]{
        if(page.selected.isEmpty()||!save_page())return;auto path=page.selected;QSettings meta(metadata(path),QSettings::IniFormat);bool archiving=!meta.value("note/archived",0).toBool();
        if(archiving&&!set_pinned(false))return;
        if(set_metadata("note/archived",int(archiving)))list_notes();
    });top->addWidget(page.archive);outer->addLayout(top);
    auto row=new QHBoxLayout;row->setSpacing(16);page.list=new QListWidget;page.list->setFixedWidth(260);page.list->setWordWrap(false);page.list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);page.list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    page.list->setStyleSheet(state.dark?"QListWidget {background:#192330;border:0;border-radius:10px;outline:0;} QListWidget::item {padding:12px;} QListWidget::item:selected {background:#345347;}":"QListWidget {background:#edf2f2;border:0;border-radius:10px;outline:0;} QListWidget::item {padding:12px;} QListWidget::item:selected {background:#cce8db;}");row->addWidget(page.list);
    auto editor=new QFrame;editor->setObjectName("surface");auto v=new QVBoxLayout(editor);v->setContentsMargins(18,18,18,18);v->setSpacing(12);
    page.title=new QLineEdit;page.title->setPlaceholderText(ui::tr("便签标题","Title"));page.title->setMaxLength(160);
    page.body=new QPlainTextEdit;page.body->setPlaceholderText(ui::tr("写下想法、清单或下一步行动…","Write an idea, a list, or your next step…"));page.body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page.body->setStyleSheet(state.dark?"QPlainTextEdit {background:#18222f;border:1px solid #344253;border-radius:10px;padding:10px;}":"QPlainTextEdit {background:#fbfdfc;border:1px solid #c9d7d0;border-radius:10px;padding:10px;}");
    v->addWidget(page.title);v->addWidget(page.body,1);
    auto options=new QHBoxLayout;page.done=new QCheckBox(ui::tr("已完成","Completed"));page.pinned=new QCheckBox(ui::tr("贴到桌面","Pin to desktop"));options->addWidget(page.done);options->addWidget(page.pinned);options->addStretch();
    page.colors=new QComboBox;page.colors->setEditable(false);page.colors->setSizeAdjustPolicy(QComboBox::AdjustToContents);page.colors->setMinimumContentsLength(9);page.colors->setIconSize(QSize(16,16));
    struct Preset{char const* zh;char const* en;char const* hex;};
    for(auto preset:{Preset{"薄荷","Mint","#a6e4ca"},Preset{"海蓝","Blue","#9ac8ff"},Preset{"暖砂","Amber","#f5d18e"},Preset{"蔷薇","Rose","#efb5c5"}})page.colors->addItem(note_swatch(QColor(preset.hex)),ui::tr(preset.zh,preset.en),rgb(QColor(preset.hex)));
    options->addWidget(page.colors);page.palette=button(ui::tr("调色板","Palette"),[]{if(page.selected.isEmpty())return;QPointer<QWidget> window(page.window);auto selected=page.selected;auto chosen=ui::dialogs::color(color(page.colors->currentData().toInt()),page.window,ui::tr("便签颜色","Note color"));if(window&&page.window==window&&page.selected==selected&&chosen.isValid()&&set_metadata("note/color",rgb(chosen))){select_note_color(rgb(chosen));list_notes();}});options->addWidget(page.palette);v->addLayout(options);
    auto reminder=new QHBoxLayout;page.has_due=new QCheckBox(ui::tr("到期提醒","Remind me"));page.due=new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600));page.due->setCalendarPopup(true);page.due->setDisplayFormat("yyyy-MM-dd HH:mm");reminder->addWidget(page.has_due);reminder->addWidget(page.due);reminder->addStretch();v->addLayout(reminder);
    page.status=label(ui::tr("自动保存到本机。设置到期提醒或贴到桌面后，关闭设置窗口仍会保留后台。","Saved automatically on this device. Reminders and desktop notes stay active after you close settings."),"muted",true);v->addWidget(page.status);row->addWidget(editor,1);outer->addLayout(row,1);
    page.save=new QTimer(root);page.save->setSingleShot(true);page.save->setInterval(300);
    QObject::connect(page.save,&QTimer::timeout,root,[]{if(save_page())list_notes();});
    QObject::connect(page.title,&QLineEdit::textChanged,root,[]{if(!page.loading){page.dirty=true;page.save->start();}});
    QObject::connect(page.body,&QPlainTextEdit::textChanged,root,[]{if(!page.loading){page.dirty=true;page.save->start();}});
    QObject::connect(page.query,&QLineEdit::textChanged,root,[]{list_notes();});QObject::connect(page.filter,&QComboBox::currentIndexChanged,root,[]{list_notes();});QObject::connect(page.list,&QListWidget::currentItemChanged,root,[]{select_item();});
    QObject::connect(page.done,&QCheckBox::toggled,root,[](bool checked){if(!page.loading&&set_metadata("note/done",int(checked)))list_notes();});
    QObject::connect(page.pinned,&QCheckBox::toggled,root,[](bool checked){if(!page.loading){set_pinned(checked);refresh_options();}});
    QObject::connect(page.colors,&QComboBox::currentIndexChanged,root,[]{if(!page.loading&&set_metadata("note/color",page.colors->currentData()))list_notes();});
    QObject::connect(page.has_due,&QCheckBox::toggled,root,[]{save_due();});QObject::connect(page.due,&QDateTimeEdit::dateTimeChanged,root,[]{save_due();});
    root->attach(page.list,page.body);list_notes();return root;
}
}
