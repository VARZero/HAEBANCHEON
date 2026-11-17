#include "frame_pacer.hpp"
#include <cassert>
#include <iostream>
using boayo::FramePacer;
int main(){
 FramePacer p;assert(p.due(0,2));p.begin(0);
 assert(!p.due(.1,2) && !p.due(.499,2));assert(p.due(.5,2));
 p.begin(.5);assert(p.interval==.5);
 // A 0.8-second render has already consumed the period: no extra 0.5 s.
 assert(p.due(1.3,2));p.begin(1.3);assert(!p.due(1.31,2));
 // Long idle/overrun never accumulates many catch-up frames.
 assert(p.due(10,2));p.begin(10);assert(!p.due(10.01,2));
 // A live rate change is evaluated against the actual last start.
 assert(p.due(10.25,4));assert(!p.due(10.25,1));
 // App work before a render also consumes the elapsed start-to-start period.
 assert(p.due(10.6,2));p.begin(10.6);assert(!p.due(10.61,2));
 std::cout<<"PASS frame pacing: work time, overrun, idle, live limit change\n";
}
