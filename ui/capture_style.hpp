#pragma once
#include <QtWidgets>
#include <initializer_list>

// Shared by screenshot annotation and recording controls. Geometry and icon paths
// follow the Python 1.0.0 capture UI; these widgets do not own capture state.
namespace capture_ui {
inline constexpr char const* style = R"QSS(
QFrame#captureStrip { background:#191c22; border:1px solid #3a414d; border-radius:9px; }
QFrame#toolDivider { background:#39414c; border:0; }
QToolButton { background:transparent; border:0; border-radius:6px; padding:2px; color:#f3f6ff; }
QToolButton:hover { background:#343c49; }
QToolButton:pressed { background:#222d3e; }
QToolButton:checked { background:#233e60; }
QToolButton:checked:hover { background:#2b4c72; }
QToolButton:disabled { color:#657080; }
QToolButton#confirmCapture { background:#2d6bd2; }
QToolButton#confirmCapture:hover { background:#3b80eb; }
QToolButton#stopRecording { background:#4c2832; }
QToolButton#stopRecording:hover { background:#6b303c; }
QLabel { color:#f3f6ff; background:transparent; border:0; }
QLabel#captureMuted { color:#9da8b9; }
QCheckBox { color:#f3f6ff; spacing:6px; background:transparent; border:0; }
QCheckBox::indicator { width:14px; height:14px; border:1px solid #586578; border-radius:4px; background:#101217; }
QCheckBox::indicator:checked { background:#4c99ff; border-color:#7db7ff; }
QCheckBox::indicator:hover { border-color:#85b6f5; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
    color:#f3f6ff; background:#101217; border:1px solid #424a57;
    border-radius:5px; padding:3px 6px; selection-background-color:#305784;
    selection-color:#ffffff; min-height:20px;
}
QComboBox { padding-right:23px; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color:#637187; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color:#4c99ff; }
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { color:#657080; border-color:#303742; }
QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:21px; border:0; background:transparent; }
QComboBox QAbstractItemView { color:#f3f6ff; background:#232833; border:1px solid #424a57;
    selection-background-color:#305784; selection-color:#ffffff; outline:0; padding:3px; }
QComboBox QAbstractItemView::item { min-height:25px; padding:2px 8px; }
QMenu { color:#f3f6ff; background:#232833; border:1px solid #424a57; border-radius:7px; padding:5px; }
QMenu::item { padding:7px 24px 7px 9px; border-radius:4px; }
QMenu::item:selected { background:#305784; }
QMenu::item:disabled { color:#657080; }
QMenu::separator { height:1px; background:#3a414d; margin:4px 5px; }
QToolTip { color:#f3f6ff; background:#232833; border:1px solid #4b5668; border-radius:4px; padding:5px 8px; }
QProgressBar { color:#d9e7fa; border:0; border-radius:4px; background:#303844; text-align:center; min-height:8px; }
QProgressBar::chunk { border-radius:4px; background:#4c99ff; }
)QSS";

inline void draw_icon(QPainter& painter, QString const& key, QColor color) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    auto line = [&](std::initializer_list<QPointF> points) {
        QPolygonF polygon;
        polygon.reserve(qsizetype(points.size()));
        for (auto const& point : points) polygon << point;
        painter.drawPolyline(polygon);
    };
    if (key == "play") line({{8,4},{20,12},{8,20},{8,4}});
    else if (key == "pause") { line({{8,5},{8,19}}); line({{16,5},{16,19}}); }
    else if (key == "stop") { painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawRoundedRect(QRectF(6,6,12,12),2,2); }
    else if (key == "select" || key == "cursor") line({{5,3},{5,20},{10,15},{14,22},{17,20},{13,14},{20,14},{5,3}});
    else if (key == "trim_start") { line({{8,5},{4,5},{4,19},{8,19}}); line({{9,12},{20,12}}); line({{16,8},{20,12},{16,16}}); }
    else if (key == "trim_end") { line({{16,5},{20,5},{20,19},{16,19}}); line({{4,12},{15,12}}); line({{8,8},{4,12},{8,16}}); }
    else if (key == "move") {
        line({{12,3},{12,21}}); line({{3,12},{21,12}}); line({{8,7},{12,3},{16,7}});
        line({{8,17},{12,21},{16,17}}); line({{7,8},{3,12},{7,16}}); line({{17,8},{21,12},{17,16}});
    } else if (key == "smart") {
        line({{3,8},{3,3},{8,3}}); line({{16,3},{21,3},{21,8}}); line({{21,16},{21,21},{16,21}});
        line({{8,21},{3,21},{3,16}}); line({{8,13},{11,16},{16,9}});
    } else if (key == "full") {
        line({{10,4},{4,4},{4,10}}); line({{14,4},{20,4},{20,10}});
        line({{20,14},{20,20},{14,20}}); line({{10,20},{4,20},{4,14}});
    } else if (key == "pen") { line({{4,16},{8,9},{12,15},{19,5}}); line({{15,5},{19,5},{20,9}}); }
    else if (key == "highlight") { line({{5,15},{15,5},{20,10},{10,20},{5,20},{5,15}}); line({{8,12},{13,17}}); line({{3,22},{15,22}}); }
    else if (key == "line") line({{4,19},{20,5}});
    else if (key == "arrow") { line({{4,19},{20,5}}); line({{13,5},{20,5},{20,12}}); }
    else if (key == "text") { line({{5,5},{19,5}}); line({{12,5},{12,20}}); line({{8,20},{16,20}}); }
    else if (key == "number") { line({{10,9},{12,7},{12,17}}); painter.drawEllipse(QRectF(3,3,18,18)); }
    else if (key == "style") {
        line({{4,7},{10,7}}); line({{14,7},{20,7}}); line({{4,17},{14,17}});
        line({{18,17},{20,17}}); line({{10,4},{10,10}}); line({{17,14},{17,20}});
    } else if (key == "record") { line({{16,9},{21,6},{21,18},{16,15}}); painter.drawRoundedRect(QRectF(3,6,13,12),2,2); }
    else if (key == "longshot") {
        line({{8,4},{4,4},{4,8}}); line({{16,4},{20,4},{20,8}}); line({{20,16},{20,20},{16,20}});
        line({{8,20},{4,20},{4,16}}); line({{12,7},{12,17}}); line({{9,14},{12,17},{15,14}});
    } else if (key == "pin") { line({{13,3},{21,11},{17,11},{12,16},{12,19},{5,12},{8,12},{13,7},{13,3}}); line({{8,15},{3,21}}); }
    else if (key == "save") { line({{12,3},{12,15}}); line({{7,10},{12,15},{17,10}}); line({{4,16},{4,21},{20,21},{20,16}}); }
    else if (key == "cancel" || key == "close") { line({{5,5},{19,19}}); line({{5,19},{19,5}}); }
    else if (key == "copy") { line({{8,4},{20,4},{20,16}}); painter.drawRoundedRect(QRectF(4,8,12,12),1,1); }
    else if (key == "undo") { line({{9,4},{4,9},{9,14}}); line({{4,9},{15,9},{19,11},{20,15},{18,19},{14,20},{8,20}}); }
    else if (key == "redo") { line({{15,4},{20,9},{15,14}}); line({{20,9},{9,9},{5,11},{4,15},{6,19},{10,20},{16,20}}); }
    else if (key == "reset") { line({{19,5},{19,10},{14,10}}); line({{19,10},{17,6},{13,4},{8,5},{5,8},{4,13},{6,17},{10,20},{15,19},{19,15}}); }
    else if (key == "crop") { line({{7,2},{7,17},{22,17}}); line({{2,7},{17,7},{17,22}}); }
    else if (key == "erase") { line({{3,15},{13,5},{20,12},{10,22},{3,15}}); line({{7,11},{14,18}}); }
    else if (key == "done") line({{4,12},{9,17},{20,6}});
    else if (key == "rect") painter.drawRoundedRect(QRectF(4,5,16,14),1.6,1.6);
    else if (key == "ellipse") painter.drawEllipse(QRectF(4,4,16,16));
    else if (key == "mosaic" || key == "mosaic_brush") {
        painter.setPen(Qt::NoPen); painter.setBrush(color);
        for (QPointF const point : {QPointF(4,4),QPointF(12,4),QPointF(8,8),QPointF(16,8),QPointF(4,12),QPointF(12,12),QPointF(8,16),QPointF(16,16)})
            painter.drawRect(QRectF(point,QSizeF(4,4)));
        if (key == "mosaic_brush") painter.drawEllipse(QRectF(3,14,6,6));
    } else if (key == "more") {
        painter.setPen(Qt::NoPen); painter.setBrush(color);
        for (int x : {5,12,19}) painter.drawEllipse(QPointF(x,12),1.4,1.4);
    } else if (key == "grip") {
        painter.setPen(Qt::NoPen); painter.setBrush(color);
        for (int x : {8,15}) for (int y : {5,12,19}) painter.drawEllipse(QPointF(x,y),1.3,1.3);
    } else if (key == "width") { line({{4,5},{20,5}}); painter.setPen(QPen(color,2.5,Qt::SolidLine,Qt::RoundCap));line({{4,12},{20,12}});painter.setPen(QPen(color,4,Qt::SolidLine,Qt::RoundCap));line({{4,20},{20,20}}); }
    else if (key == "radius") { painter.setPen(QPen(color,1.75,Qt::DashLine,Qt::RoundCap));line({{3,20},{3,4},{12,4}});painter.setPen(QPen(color,1.75,Qt::SolidLine,Qt::RoundCap));painter.drawArc(QRectF(5,4,15,15),0,90*16);line({{20,12},{20,20},{3,20}}); }
}

inline QIcon icon(QString key, bool grouped=false) {
    QIcon result;
    auto add = [&](QIcon::Mode mode,QIcon::State state,QColor color) {
        QPixmap image(grouped?66:48,48);
        image.fill(Qt::transparent);
        QPainter painter(&image); painter.scale(2,2); draw_icon(painter,key,color);
        if (grouped) {
            painter.setPen(QPen(mode==QIcon::Disabled?QColor("#596372"):QColor("#aab3c0"),1.4,Qt::SolidLine,Qt::RoundCap));
            painter.setBrush(Qt::NoBrush);painter.drawPolyline(QPolygonF{QPointF(24,11),QPointF(27,14),QPointF(30,11)});
        }
        painter.end();image.setDevicePixelRatio(2);result.addPixmap(image,mode,state);
    };
    add(QIcon::Normal,QIcon::Off,QColor("#f3f6ff"));
    add(QIcon::Normal,QIcon::On,QColor("#4c99ff"));
    add(QIcon::Active,QIcon::Off,QColor("#ffffff"));
    add(QIcon::Active,QIcon::On,QColor("#74b1ff"));
    add(QIcon::Disabled,QIcon::Off,QColor("#606b7a"));
    add(QIcon::Disabled,QIcon::On,QColor("#606b7a"));
    return result;
}

inline void apply(QWidget* widget) {
    widget->setStyleSheet(QString::fromLatin1(style));
    widget->setFont(QFont(QStringLiteral("Microsoft YaHei UI"),9));
    QPalette palette=widget->palette();
    palette.setColor(QPalette::Window,QColor("#191c22"));
    palette.setColor(QPalette::Base,QColor("#101217"));
    palette.setColor(QPalette::AlternateBase,QColor("#232833"));
    palette.setColor(QPalette::Button,QColor("#232833"));
    palette.setColor(QPalette::Text,QColor("#f3f6ff"));
    palette.setColor(QPalette::WindowText,QColor("#f3f6ff"));
    palette.setColor(QPalette::ButtonText,QColor("#f3f6ff"));
    palette.setColor(QPalette::Highlight,QColor("#305784"));
    palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor("#657080"));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#657080"));
    widget->setPalette(palette);
}

// With a styled drop-down some Windows Qt styles omit their native arrow. Paint
// the affordance ourselves after the standard widget, including font choices.
// The base class still owns its popup, keyboard navigation and accessibility.
template<class Base> class ArrowChoice : public Base {
public:
    explicit ArrowChoice(QWidget* parent=nullptr):Base(parent) { this->setEditable(false); }
protected:
    void paintEvent(QPaintEvent* event) override {
        Base::paintEvent(event);
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(this->isEnabled()?QColor("#aab3c0"):QColor("#596372"),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const qreal x=this->layoutDirection()==Qt::RightToLeft?12:this->width()-12;
        const qreal y=this->height()/2.0;
        painter.drawPolyline(QPolygonF{QPointF(x-3.5,y-1.5),QPointF(x,y+2),QPointF(x+3.5,y-1.5)});
    }
};
using ComboBox=ArrowChoice<QComboBox>;
using FontComboBox=ArrowChoice<QFontComboBox>;

inline QFrame* strip(QWidget* parent=nullptr) {
    auto frame=new QFrame(parent);frame->setObjectName(QStringLiteral("captureStrip"));
    frame->setAttribute(Qt::WA_StyledBackground);frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    auto layout=new QHBoxLayout(frame);layout->setContentsMargins(5,4,5,4);layout->setSpacing(3);
    return frame;
}
inline QHBoxLayout* row(QFrame* frame) { return static_cast<QHBoxLayout*>(frame->layout()); }
inline QToolButton* button(QString key,QString tooltip,QWidget* parent=nullptr,bool grouped=false) {
    auto item=new QToolButton(parent);item->setIcon(icon(key,grouped));item->setIconSize(QSize(grouped?29:21,21));
    item->setFixedSize(grouped?37:32,32);item->setToolTip(tooltip);item->setAccessibleName(tooltip);
    item->setCursor(Qt::PointingHandCursor);item->setFocusPolicy(Qt::NoFocus);item->setAutoRaise(true);
    return item;
}
inline QFrame* divider(QWidget* parent=nullptr) {
    auto item=new QFrame(parent);item->setObjectName(QStringLiteral("toolDivider"));item->setFixedSize(1,20);return item;
}
inline void setSwatch(QToolButton* button,QColor color,bool selected=false,bool custom=false) {
    button->setStyleSheet(QStringLiteral("QToolButton { background:%1; border:%2px solid %3; border-radius:%4px; padding:0; } QToolButton:hover { border-color:#c4dafd; }")
        .arg(color.name(QColor::HexRgb)).arg(selected||custom?2:1).arg(selected||custom?QStringLiteral("#81acff"):QStringLiteral("#596472")).arg(custom?6:5));
}
inline QToolButton* colorButton(QColor color,QString tooltip,QWidget* parent=nullptr,bool custom=false) {
    auto item=new QToolButton(parent);item->setFixedSize(custom?28:21,custom?26:21);item->setCursor(Qt::PointingHandCursor);
    item->setFocusPolicy(Qt::NoFocus);item->setToolTip(tooltip);item->setAccessibleName(tooltip);setSwatch(item,color,false,custom);return item;
}
inline QStringList colors() { return {QStringLiteral("#ff6868"),QStringLiteral("#ffd382"),QStringLiteral("#a6e4ca"),QStringLiteral("#68adff"),QStringLiteral("#151515"),QStringLiteral("#ffffff")}; }
}
