#pragma once
#include "lb.h"
#include <optional>
namespace lb::scroll {
// Keep the Python 1.0.0 capture limit without allocating the entire budget up
// front. Sampling, the pending queue and undo retain their separate bounds.
inline constexpr size_t maximum_pixels=40'000'000;
constexpr bool fits_pixel_budget(int width,int height) {
    return width>0&&height>0&&size_t(width)<=maximum_pixels/size_t(height);
}
struct Frame { int width{},height{};std::vector<uint32_t> pixels; };
std::optional<int> match(Frame const& previous,Frame const& current);
class Stitch {
public:
    enum class Change { added,unchanged,unmatched,limit };
private:
    Frame previous_;
    int position_{};
    struct Undo { Frame previous;int position{},height{},prepend{},append{},header{},footer{};std::vector<uint32_t> edge; };
    std::optional<Undo> undo_;
    Change append_impl(Frame frame,std::optional<int> forced);
public:
    int width{},height{};
    std::vector<uint32_t> pixels;
    Change append(Frame frame);
    Change page(Frame frame);
    Change manual(Frame frame,int overlap);
    bool can_undo() const { return undo_.has_value(); }
    Frame const& current_frame() const { return previous_; }
    bool undo();
};
// The first frame is copied before the selection overlay disappears. No pointer
// or bitmap ownership crosses the asynchronous worker boundary.
bool start(RECT region,HBITMAP first=nullptr);
bool shutdown();
bool tests();
bool session_tests();
bool finish_tests();
bool controls_tests();
bool source_tests();
bool ui_bridge_tests();
}
