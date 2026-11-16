#pragma once
#include "lb.h"
namespace lb::particles {
struct Particle { double x{},y{},vx{},vy{},age{},life{},size{},phase{}; };
class Model {
    POINT previous_{};bool initialized_{};unsigned serial_{};
public:
    int style{};std::vector<Particle> items;
    void move(POINT point,double elapsed);
    void advance(double dt);
    void clear() { items.clear();initialized_=false; }
};
bool configure();
void shutdown();
bool tests();
bool reference_tests();
}
