#pragma once
#include <cstdint>
#include <initializer_list>

namespace lb::capture {
// Wait after hiding our windows, then observe two distinct compositor frames.
// A single fixed 120 ms timer could capture Windows' outgoing window shadow.
struct StartGate {
    bool pending{},record{};
    uint64_t requested{},presented_at{};
    unsigned frames{};
    bool arm(uint64_t now,bool recording) {
        if(pending) return false;
        pending=true;record=recording;requested=now;presented_at=0;frames=0;return true;
    }
    bool due(uint64_t now) const {return pending&&now-requested>=180&&(!frames||now-presented_at>=32);}
    bool presented(uint64_t now) {
        if(!due(now)) return false;
        presented_at=now;
        if(++frames<2) return false;
        pending=false;return true;
    }
    void cancel() {pending=false;frames=0;}
};
inline bool start_gate_tests() {
    for(uint64_t interval:{uint64_t(8),uint64_t(25),uint64_t(100)}) {
        StartGate gate;if(!gate.arm(1000,true)||gate.arm(1020,false)||!gate.record||gate.presented(1179))return false;
        uint64_t first=0,last=0;for(uint64_t now=1000;now<2000;now+=interval) if(gate.due(now)) {
            if(!first)first=now;if(gate.presented(now)){last=now;break;}
        }
        if(!last||first<1180||last-first<32||gate.pending||gate.presented(2100))return false;
        gate.arm(2200,false);gate.cancel();if(gate.presented(3000)||gate.due(3000)||!gate.arm(3100,false)||gate.record)return false;
    }
    return true;
}
}
