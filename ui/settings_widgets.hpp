#pragma once
#include "ui.hpp"

namespace ui {
// Stable left label / right control columns keep every settings row legible at
// different language lengths. Short toggles stay at the same trailing edge.
class SettingsRow final:public QFrame {
public:
    QLabel* name{};QWidget* value{};
    SettingsRow(QString title,QWidget* control,QWidget* parent=nullptr):QFrame(parent),value(control){
        setObjectName("settingsRow");setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);setMinimumHeight(64);
        auto row=new QHBoxLayout(this);row->setContentsMargins(2,12,2,12);row->setSpacing(24);
        name=new QLabel(std::move(title));name->setWordWrap(true);name->setMinimumWidth(120);name->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);row->addWidget(name,1);
        auto slot=new QWidget;slot->setFixedWidth(280);auto contents=new QHBoxLayout(slot);contents->setContentsMargins(0,0,0,0);contents->setSpacing(0);
        bool compact=control->maximumWidth()<=64;if(compact)contents->addStretch();else control->setMinimumWidth(0);
        contents->addWidget(control,compact?0:1,Qt::AlignVCenter);row->addWidget(slot,0,Qt::AlignVCenter);
    }
};
// QToolButton centers its icon/text despite text-align in several Qt styles.
// Explicit painting keeps a whole-row click target with a stable left edge.
class SectionButton final : public QToolButton {
public:
    explicit SectionButton(QWidget* parent=nullptr):QToolButton(parent) {
        setCheckable(true);setChecked(true);setArrowType(Qt::DownArrow);
        setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setMinimumHeight(36);
        setStyleSheet("QToolButton{background:transparent;border:0;padding:0;}");
    }
    QSize sizeHint()const override {auto f=font();f.setPixelSize(15);f.setWeight(QFont::DemiBold);return {QFontMetrics(f).horizontalAdvance(text())+38,36};}
    void paintEvent(QPaintEvent*)override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        if(underMouse()||hasFocus()){p.setPen(hasFocus()?QPen(palette().color(QPalette::Highlight),1):QPen(Qt::NoPen));auto c=palette().color(QPalette::WindowText);c.setAlpha(underMouse()?12:5);p.setBrush(c);p.drawRoundedRect(rect().adjusted(0,1,-1,-1),6,6);}
        p.setPen(QPen(palette().color(QPalette::WindowText),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        qreal cy=height()/2.;if(isChecked())p.drawPolyline(QPolygonF{{7,cy-2},{11,cy+2},{15,cy-2}});else p.drawPolyline(QPolygonF{{9,cy-4},{13,cy},{9,cy+4}});
        auto f=font();f.setPixelSize(15);f.setWeight(QFont::DemiBold);p.setFont(f);
        p.drawText(QRect(25,0,width()-30,height()),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(text(),Qt::ElideRight,width()-30));
    }
};
class SettingsCombo final : public QComboBox {
public:
    explicit SettingsCombo(QWidget* parent=nullptr):QComboBox(parent){setObjectName("settingsCombo");setEditable(false);setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);setMinimumContentsLength(10);connect(this,&QComboBox::currentIndexChanged,this,[this]{setToolTip(currentText());});}
protected:
    void paintEvent(QPaintEvent*)override{QStylePainter base(this);QStyleOptionComboBox option;initStyleOption(&option);base.drawComplexControl(QStyle::CC_ComboBox,option);auto edit=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxEditField,this);option.currentText=fontMetrics().elidedText(option.currentText,Qt::ElideRight,std::max(12,edit.width()-4));base.drawControl(QStyle::CE_ComboBoxLabel,option);base.end();QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(palette().color(isEnabled()?QPalette::Text:QPalette::PlaceholderText),1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));auto x=width()-14.,y=height()/2.;p.drawPolyline(QPolygonF{{x-3.5,y-1.5},{x,y+2},{x+3.5,y-1.5}});}
    void showPopup()override{int longest=0;QFontMetrics m(font());for(int n=0;n<count();++n)longest=std::max(longest,m.horizontalAdvance(itemText(n)));view()->setMinimumWidth(std::min(600,std::max(width(),longest+38)));QComboBox::showPopup();}
};
// Keep the native step hit regions and keyboard/accessibility behavior, but draw
// clear vector chevrons: the Fusion glyph can collapse to a dot under custom QSS.
template<class Base> class SettingsSpinControl final : public Base {
public:
    explicit SettingsSpinControl(QWidget* parent=nullptr):Base(parent){this->setObjectName("settingsSpin");}
    QRect stepArea(bool up)const{QStyleOptionSpinBox option;this->initStyleOption(&option);return this->style()->subControlRect(QStyle::CC_SpinBox,&option,up?QStyle::SC_SpinBoxUp:QStyle::SC_SpinBoxDown,this);}
protected:
    void paintEvent(QPaintEvent* event)override{
        Base::paintEvent(event);QStyleOptionSpinBox option;this->initStyleOption(&option);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        for(auto control:{QStyle::SC_SpinBoxUp,QStyle::SC_SpinBoxDown}){
            auto area=this->style()->subControlRect(QStyle::CC_SpinBox,&option,control,this);if(area.isEmpty())continue;
            bool enabled=this->isEnabled()&&bool(option.stepEnabled&(control==QStyle::SC_SpinBoxUp?QAbstractSpinBox::StepUpEnabled:QAbstractSpinBox::StepDownEnabled));
            auto color=this->palette().color(QPalette::Text);if(!enabled)color.setAlpha(90);painter.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            QPointF center=QRectF(area).center();qreal sign=control==QStyle::SC_SpinBoxUp?-1.:1.;painter.drawPolyline(QPolygonF{{center.x()-3.5,center.y()-sign*1.8},{center.x(),center.y()+sign*1.8},{center.x()+3.5,center.y()-sign*1.8}});
        }
    }
};
using SettingsSpinBox=SettingsSpinControl<QSpinBox>;
using SettingsDoubleSpinBox=SettingsSpinControl<QDoubleSpinBox>;
inline QIcon github_mark(bool dark){QPixmap pix(48,48);pix.fill(Qt::transparent);QPainter painter(&pix);painter.setRenderHint(QPainter::Antialiasing);painter.scale(3,3);QPainterPath path;path.moveTo(6.76562,11.3281);path.cubicTo(4.70312,11.0781,3.25,9.59375,3.25,7.67188);path.cubicTo(3.25,6.89063,3.53125,6.04688,4,5.48438);path.cubicTo(3.79688,4.96875,3.82813,3.875,4.0625,3.42188);path.cubicTo(4.6875,3.34375,5.53125,3.67188,6.03125,4.125);path.cubicTo(6.625,3.9375,7.25,3.84375,8.01562,3.84375);path.cubicTo(8.78125,3.84375,9.40625,3.9375,9.96875,4.10938);path.cubicTo(10.4531,3.67188,11.3125,3.34375,11.9375,3.42188);path.cubicTo(12.1562,3.84375,12.1875,4.9375,11.9844,5.46875);path.cubicTo(12.4844,6.0625,12.75,6.85938,12.75,7.67188);path.cubicTo(12.75,9.59375,11.2969,11.0469,9.20312,11.3125);path.cubicTo(9.73438,11.6562,10.0938,12.4062,10.0938,13.2656);path.lineTo(path.currentPosition().x(),14.8906);path.cubicTo(10.0938,15.3594,10.4844,15.625,10.9531,15.4375);path.cubicTo(13.7812,14.3594,16,11.5312,16,8.03125);path.cubicTo(16,3.60938,12.4062,0,7.98438,0);path.cubicTo(3.5625,0,0,3.60938,0,8.03125);path.cubicTo(0,11.5,2.20312,14.375,5.17188,15.4531);path.cubicTo(5.59375,15.6094,6,15.3281,6,14.9062);path.lineTo(path.currentPosition().x(),13.6563);path.cubicTo(5.78125,13.75,5.5,13.8125,5.25,13.8125);path.cubicTo(4.21875,13.8125,3.60938,13.25,3.17188,12.2031);path.cubicTo(3,11.7812,2.8125,11.5313,2.45312,11.4844);path.cubicTo(2.26562,11.4688,2.20312,11.3906,2.20312,11.2969);path.cubicTo(2.20312,11.1094,2.51562,10.9688,2.82812,10.9688);path.cubicTo(3.28125,10.9688,3.67188,11.25,4.07812,11.8281);path.cubicTo(4.39062,12.2813,4.71875,12.4844,5.10938,12.4844);path.cubicTo(5.5,12.4844,5.75,12.3438,6.10938,11.9844);path.cubicTo(6.375,11.7188,6.57812,11.4844,6.76562,11.3281);path.closeSubpath();painter.fillPath(path,QColor(dark?"#f0f5f2":"#26343b"));painter.end();pix.setDevicePixelRatio(2);return QIcon(pix);}

}
