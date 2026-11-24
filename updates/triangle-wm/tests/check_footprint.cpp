#define TRIANGLE_ENGINE_ONLY
#include MARKER_ENGINE_HEADER
#include "reference_footprint.inc"
#include <cstdio>
#include <cassert>
int main(){
 Engine e(true);uint32_t seed=0x1511;
 auto next=[&](){return seed=seed*1664525u+1013904223u;};
 for(unsigned i=0;i<1024;i++){
  Window w{};w.yaw=Fx::ratio(int(next()%1441)-720,4);w.pitch=Fx::ratio(int(next()%641)-320,4);
  w.width=Fx::ratio(32+next()%369,4);w.height=Fx::ratio(24+next()%377,4);w.padding=Fx::ratio(next()%49,4);
  for(bool trim:{false,true}){
   e.trim_background=trim;Window original=w;
   auto expected=flat_reference(e,original);auto actual=e.footprint(w);assert(actual==expected);
   assert(e.footprint(w)==flat_reference(e,original));
  }
 }
 std::puts("PASS_FOOTPRINT_HIERARCHY: 1024 poses/sizes/paddings, trimmed/raw and cached transforms exactly match original flat scan");
}
