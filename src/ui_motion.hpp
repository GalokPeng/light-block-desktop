#pragma once
#include <algorithm>
#include <cmath>
namespace lb::motion {
// Analytic critically damped spring: time-step independent, interruptible and
// bounded. Preserve current value/velocity when the user changes direction.
struct Spring {
    double value{},velocity{},target{};double response{32};
    void reset(double v){value=target=v;velocity=0;}
    void aim(double v){target=v;}
    bool step(double seconds,bool reduced=false){
        if(reduced||seconds>.25){reset(target);return false;}
        seconds=std::clamp(seconds,0.,.25);double offset=value-target,b=velocity+response*offset,decay=std::exp(-response*seconds);
        value=target+(offset+b*seconds)*decay;velocity=(velocity-response*b*seconds)*decay;
        if(std::abs(value-target)<.12&&std::abs(velocity)<2){reset(target);return false;}return true;
    }
};
inline bool tests(){
    Spring s;s.reset(100);s.aim(600);s.step(.05);double value=s.value,velocity=s.velocity;s.aim(100);
    if(s.value!=value||s.velocity!=velocity)return false;
    for(int n=0;n<120;++n){s.step(1./120);if(!std::isfinite(s.value)||s.value<99||s.value>601)return false;}
    if(s.value!=100||s.velocity!=0)return false;s.aim(600);if(s.step(.01,true)||s.value!=600)return false;
    Spring a,b;a.aim(1);b.aim(1);for(int i=0;i<12;++i)a.step(1./120);for(int i=0;i<6;++i)b.step(1./60);
    if(std::abs(a.value-b.value)>.00001)return false;b.aim(0);return !b.step(1)&&b.value==0;
}
}
