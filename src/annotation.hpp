#pragma once
#include "lb.h"
#include <deque>
#include <functional>
#include <optional>

namespace lb::capture {
enum class Tool { select,rectangle,ellipse,line,arrow,pen,highlight,text,number,mosaic,mosaic_brush,eraser,sequence,image_crop };
struct Style {
    COLORREF color{RGB(247,91,86)};
    int width{3},radius{},font_size{24},number{1},block_size{14};
    bool filled{},dashed{},bold{},italic{},underline{};
    std::wstring font{L"Segoe UI"};
    bool operator==(Style const&) const = default;
};
struct Annotation {
    Tool tool{Tool::rectangle};Style style;
    std::vector<POINT> points;
    std::wstring text;
    RECT bounds() const;
    void translate(int x,int y);
    bool operator==(Annotation const& other) const {return tool==other.tool&&style==other.style&&text==other.text&&points.size()==other.points.size()&&std::equal(points.begin(),points.end(),other.points.begin(),[](POINT a,POINT b){return a.x==b.x&&a.y==b.y;});}
};
class Document {
    unique_gdi<HBITMAP> original_,composite_;
    std::deque<std::vector<Annotation>> history_,future_;
    std::optional<std::vector<Annotation>> pending_before_;
    bool dirty_{true};int render_skip_{-1};
    void trim_history();
public:
    struct History { std::deque<std::vector<Annotation>> past,future; };
    int width{},height{};
    std::vector<Annotation> items;
    explicit Document(HBITMAP source);
    void checkpoint();
    void finish_edit();
    bool undo();
    bool redo();
    bool can_undo() const noexcept { return !history_.empty()||(pending_before_&&*pending_before_!=items); }
    bool can_redo() const noexcept { return !future_.empty()&&(!pending_before_||*pending_before_==items); }
    History history() const { return {history_,future_}; }
    History take_history(){finish_edit();return {std::move(history_),std::move(future_)};}
    void restore_history(History history) {pending_before_.reset();history_=std::move(history.past);future_=std::move(history.future);trim_history();}
    size_t history_bytes() const;
    void changed() { dirty_=true; }
    HBITMAP render(int skip=-1);
    int hit(POINT point,double margin=6.,bool include_mosaic=true,int skip=-1) const;
    bool begin_erase(POINT point,int diameter);
    bool extend_erase(POINT point);
};
unique_gdi<HBITMAP> crop(HBITMAP source,RECT bounds);
unique_gdi<HBITMAP> overlay(int width,int height,std::vector<Annotation> const& items);
HWND edit_project(HBITMAP bitmap,RECT location,std::vector<Annotation> items,std::function<void(std::vector<Annotation>)> complete,Tool initial=Tool::pen,Document::History history={},std::function<void(Document::History)> retain_history={});
bool annotation_tests();
bool eraser_tests();
bool hit_tests();
bool history_budget_tests();
bool wheel_tests();
bool coordinate_tests();
bool editor_tests();
bool selection_tests();
bool selection_visual_tests();
bool visual_parity_tests();
bool dialog_tests(std::wstring const& directory);
bool save_tests(std::wstring const& directory);
void edit_clipboard(HWND owner);
}
