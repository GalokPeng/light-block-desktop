#pragma once
#include "../src/ui_api.h"
#include <QtWidgets>
#include <windows.h>
#include <dwmapi.h>
#include <memory>
namespace ui {
extern UiApi api;
extern UiState state;
inline QString s(wchar_t const* text) { return QString::fromUtf16(reinterpret_cast<char16_t const*>(text)); }
inline wchar_t const* w(QString const& text) { return reinterpret_cast<wchar_t const*>(text.utf16()); }
inline QString tr(char const* zh,char const* en) { return QString::fromUtf8(state.english?en:zh); }
inline QColor color(int value) { return QColor(value&255,(value>>8)&255,(value>>16)&255); }
inline int rgb(QColor const& c) { return c.red()|(c.green()<<8)|(c.blue()<<16); }
QIcon icon(int index,bool light=false);
QPushButton* button(QString text,std::function<void()> action,QWidget* owner=nullptr,char const* name=nullptr);
QLabel* label(QString text,char const* name=nullptr,bool wrap=false);
void refresh_notes();
void note_open(wchar_t const* file,int activate);
void note_close(wchar_t const* file);
int notes_shutdown(int preserve);
QWidget* notes_page(QWidget* parent=nullptr);
void notes_refresh_page();
bool notes_flush();
bool notes_ui_tests(QString const& directory);
void desktop_layer(HWND window);
}
