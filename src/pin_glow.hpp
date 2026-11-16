#pragma once
#include "lb.h"
#include <array>

namespace lb::capture {
// Four narrow alpha windows avoid a full-image backing bitmap just for a glow.
// They belong to the pin, never activate and pass mouse input through.
class PinGlow {
    std::array<HWND,4> windows_{};
    int width_{},height_{},margin_{};
    BYTE alpha_{};
    bool clipped_{},updating_{};
public:
    PinGlow()=default;
    PinGlow(PinGlow const&)=delete;
    PinGlow& operator=(PinGlow const&)=delete;
    ~PinGlow();
    void reset();
    bool update(HWND owner,bool border,int margin,double opacity);
    static bool tests();
};
}
