#define TRIANGLE_ENGINE_ONLY
#include "../src/triangle_wm.cpp"
#include <random>

int main(){
 Engine e(true);std::mt19937 rng(73441);unsigned removed=0,changed=0;
 for(unsigned n=0;n<600;n++){
  Window w{};
  w.yaw=n<4?(n%2?179.:-179.):double(int(rng()%36001)-18000)/100;
  w.pitch=n<4?(n<2?80.:-80.):double(int(rng()%16001)-8000)/100;
  w.width=1+double(rng()%9900)/100;w.height=1+double(rng()%9900)/100;
  w.padding=n%5?2.:0.;
  e.trim_background=false;auto raw=e.footprint(w);
  Window inner=w;inner.padding=0;auto required=e.footprint(inner);
  e.trim_background=true;auto trimmed=e.footprint(w);
  require(std::is_sorted(trimmed.begin(),trimmed.end()),"sorted footprint");
  require(std::includes(raw.begin(),raw.end(),trimmed.begin(),trimmed.end()),"trim grew footprint");
  for(unsigned t:required)if(std::binary_search(raw.begin(),raw.end(),t))
   require(std::binary_search(trimmed.begin(),trimmed.end(),t),"content footprint lost");
  if(w.padding==0)require(raw==trimmed,"zero padding changed");
  auto projection=w.transform.projection(240,144);
  for(unsigned t:raw)if(!std::binary_search(trimmed.begin(),trimmed.end(),t)){
   require(!e.geometry.descriptor_reference(t,projection).second,"visible content tile removed");removed++;
  }
  auto again=trimmed;
  trim_outline(again,[&](unsigned t){return std::binary_search(required.begin(),required.end(),t);});
  require(again==trimmed,"outline not stable");
  changed+=raw!=trimmed;
 }
 Window initial{};e.trim_background=false;auto before=e.footprint(initial);
 e.trim_background=true;auto after=e.footprint(initial);
 require(before.size()==72&&after.size()==70,"default outline changed unexpectedly");
 std::cout<<"PASS_OUTLINE 600 windows, changed="<<changed<<", removed="<<removed<<", default=72->70, content-reference-safe, seam/pole/zero-padding/idempotence"<<std::endl;
}
