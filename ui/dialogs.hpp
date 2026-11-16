#pragma once
#include "ui.hpp"
namespace ui::dialogs {
// Fixture seams let the actual button callbacks exercise modal reentrancy
// without displaying a system dialog or reading the user's selected files.
inline std::function<QColor(QColor const&,QWidget*,QString const&)> color_fixture;
inline std::function<QString(int,QWidget*)> path_fixture;
inline QColor color(QColor const& initial,QWidget* parent,QString const& title){return color_fixture?color_fixture(initial,parent,title):QColorDialog::getColor(initial,parent,title);}
inline QString open(QWidget* parent,QString const& title,QString const& directory,QString const& filter){return path_fixture?path_fixture(0,parent):QFileDialog::getOpenFileName(parent,title,directory,filter);}
inline QString save(QWidget* parent,QString const& title,QString const& directory,QString const& filter,QString* selected=nullptr,QFileDialog::Options options={}){return path_fixture?path_fixture(1,parent):QFileDialog::getSaveFileName(parent,title,directory,filter,selected,options);}
inline QString folder(QWidget* parent,QString const& title,QString const& directory={}){return path_fixture?path_fixture(2,parent):QFileDialog::getExistingDirectory(parent,title,directory);}
}
