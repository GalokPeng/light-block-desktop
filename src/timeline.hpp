#pragma once
#include "lb.h"
#include <cmath>

namespace lb {
struct Timeline {
    enum class Handle { start,end,play };
    double fps{24};
    int64_t duration{},frames{},first{},last{},position{};
    Handle active{Handle::play};
    void reset(int64_t milliseconds,double frame_rate) {
        fps=std::clamp(frame_rate,1.,240.);duration=std::clamp<int64_t>(milliseconds,0,24LL*60*60*1000);
        frames=int64_t(std::ceil(double(duration)*fps/1000));first=0;last=frames;position=0;
    }
    int64_t milliseconds(int64_t frame) const { return std::min(duration,int64_t(std::llround(double(frame)*1000/fps))); }
    int64_t frame_at(int x,int width) const { return std::clamp<int64_t>(std::llround(double(x-12)*frames/std::max(1,width-24)),0,frames); }
    int x(int64_t frame,int width) const { return 12+int(std::llround(double(width-24)*frame/std::max<int64_t>(1,frames))); }
    void apply(int64_t frame) {
        if(!frames) return;
        if(active==Handle::start) { first=std::clamp<int64_t>(frame,0,last-1);position=first; }
        else if(active==Handle::end) { last=std::clamp<int64_t>(frame,first+1,frames);position=last-1; }
        else position=std::clamp<int64_t>(frame,first,last-1);
    }
    void step(bool right,bool shift) { auto frame=active==Handle::start?first:active==Handle::end?last:position;apply(frame+(right?1:-1)*(shift?10:1)); }
};
HWND timeline_create(HWND parent,RECT bounds,Timeline *model,UINT change);
bool timeline_tests();
}
