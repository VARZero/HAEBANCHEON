// Independent byte-level reference for the frozen single-tile compositor.
#define TRIANGLE_ENGINE_ONLY
#ifndef MARKER_ENGINE_HEADER
#define MARKER_ENGINE_HEADER "../src/triangle_wm.cpp"
#endif
#include MARKER_ENGINE_HEADER
#include <cassert>
int main(){
 Engine e(true);for(unsigned c:{84u,164u,109u,38u,228u,255u})e.solid(c);
 e.ui_enabled=true;e.pointer.visible=false;e.operate({{"op","create"},{"id",1}});
 auto&w=e.windows.at(1);e.attach(w,{{"width",64},{"height",48},{"components",J::array({{{"id",1},{"x",0},{"y",0},{"w",32},{"h",48},{"color",255}}})}});
 e.operate({{"op","create"},{"id",2},{"yaw",8},{"color",164}});
 assert(Engine::marker_cells==1024);
 e.ui_layout_dirty=true;e.ui_dirty=true;e.render(false,true);
 auto original=e.snapshot();unsigned initial=e.ui_pick(640,360);
 e.marker_begin(2,2,initial,228);e.render(false,true);
 auto frozen=e.move_marker.base;std::map<unsigned,std::array<uint8_t,1024>> source;
 for(unsigned t=0;t<tiles;t++)if(frozen[256+t]!=UINT32_MAX&&!source.count(frozen[256+t])){
  auto*p=reinterpret_cast<const uint8_t*>(e.mock_memory[BO_SCENE0+e.active].data()+header)+frozen[256+t];std::copy_n(p,1024,source[frozen[256+t]].begin());
 }
 auto plans=e.plan_builds,footprints=e.footprint_calls,runs=e.run_submissions;
 auto verify=[&]{
  unsigned bank=e.active;auto*memory=reinterpret_cast<const uint8_t*>(e.mock_memory[BO_SCENE0+bank].data()+header);
  for(unsigned t=0;t<tiles;t++){
   bool marked=e.move_marker.drawn&&t==e.move_marker.tile;unsigned off=e.display_metadata[256+t];
   assert(off==(marked?e.colors.at(228)*1024:frozen[256+t]));
   if(off==UINT32_MAX)continue;
   auto found=source.find(frozen[256+t]);
   for(unsigned cell=0;cell<1024;cell++){
    unsigned expected=found==source.end()?0:found->second[cell];
    if(marked)expected=228;
    assert(memory[off+cell]==expected);
   }
  }
 };
 verify();std::vector<unsigned> route;
 for(unsigned t=0;t<tiles;t+=37)route.push_back(t);
 for(unsigned i=0;i<2;i++)for(unsigned t:route){if(e.marker_update(t))e.render_marker();verify();
  assert(e.plan_builds==plans&&e.footprint_calls==footprints&&e.run_submissions==runs);
  assert(e.stats.projected==0&&e.stats.plan_tiles==0&&e.ui_masks_us==0);
 }
 // All marked triangles reuse the same solid payload, without new pixel writes.
 std::vector<unsigned> background;for(unsigned t=0;t<tiles&&background.size()<10;t++)if(frozen[256+t]==UINT32_MAX)background.push_back(t);
 for(unsigned t:background)if(e.marker_update(t))e.render_marker();auto builds=e.move_marker.payload_builds;
 for(unsigned t:background)if(e.marker_update(t))e.render_marker();assert(e.move_marker.payload_builds==builds&&builds==0&&e.move_marker.payload_reuses>0&&e.move_marker.last_payload_ops==0);
 e.marker_cancel();e.render(false,true);assert(e.snapshot()==original);
 // Resize paints complete boundary triangles with one immutable solid payload.
 // Every byte of every tile is checked over changing rings and alternating banks.
 auto&resize=e.windows.at(1);e.marker_begin(1,3,initial,228);e.marker_resize(resize,true);e.render(false,true);
 auto base=e.move_marker.base;source.clear();
 for(unsigned t=0;t<tiles;t++)if(base[256+t]!=UINT32_MAX&&!source.count(base[256+t])){
  auto*p=reinterpret_cast<const uint8_t*>(e.mock_memory[BO_SCENE0+e.active].data()+header)+base[256+t];std::copy_n(p,1024,source[base[256+t]].begin());
 }
 auto ring_reference=[&](const std::vector<unsigned>&ids){
  return std::set<unsigned>(ids.begin(),ids.end());
 };
 auto verify_ring=[&](const std::set<unsigned>&ring){
  assert(std::set<unsigned>(e.move_marker.rim.begin(),e.move_marker.rim.end())==ring);
  auto*memory=reinterpret_cast<const uint8_t*>(e.mock_memory[BO_SCENE0+e.active].data()+header);
  for(unsigned t=0;t<tiles;t++){
   bool marked=ring.count(t)&&!e.move_marker.occluded[t];unsigned off=e.display_metadata[256+t];
   assert(off==(marked?e.colors.at(228)*1024:base[256+t]));if(off==UINT32_MAX)continue;
   auto found=source.find(base[256+t]);for(unsigned cell=0;cell<1024;cell++){
    unsigned expected=marked?228:found==source.end()?0:found->second[cell];assert(memory[off+cell]==expected);
   }
  }
  assert(e.move_marker.last_payload_ops==0&&e.move_marker.payload_builds==0);
  assert(e.plan_builds==plans&&e.run_submissions==runs);
 };
 verify_ring(ring_reference(resize.footprint));assert(e.move_marker.rim.size()>4);
 auto fill=e.fill_bytes;
 for(unsigned i=0;i<28;i++){
  Window target{};target.width=10+i%9*2;target.height=8+i%7*2;target.yaw=(int(i%5)-2)*2;target.pitch=int(i%3)-1;target.padding=resize.padding;
  Window reference{};reference.width=target.width;reference.height=target.height;reference.yaw=target.yaw;reference.pitch=target.pitch;reference.padding=target.padding;
  auto expected=ring_reference(e.footprint(reference));
  if(e.marker_resize(target))e.render_marker();verify_ring(expected);
  assert(!e.marker_resize(target));assert(e.fill_bytes==fill);
 }
 e.marker_cancel();e.render(false,true);assert(e.snapshot()==original);
 // A higher window hides the lower window's representative tile.
 unsigned covered=tiles;for(unsigned t=0;t<tiles;t++)if(e.planned_owner[t]==2){covered=t;break;}assert(covered<tiles);
 e.marker_begin(1,2,covered,228);e.render(false,true);assert(!e.move_marker.drawn);assert(e.snapshot()==original);
 e.marker_cancel();e.render(false,true);assert(e.snapshot()==original);
 // A completely hidden content window must be revealed when the resize shrinks.
 Engine behind(true);for(unsigned c:{84u,164u,109u,38u,228u,255u})behind.solid(c);
 behind.ui_enabled=true;behind.pointer.visible=false;
 behind.operate({{"op","create"},{"id",1}});behind.attach(behind.windows.at(1),{{"width",32},{"height",32},{"components",J::array({{{"id",1},{"x",0},{"y",0},{"w",32},{"h",32},{"color",38}}})}});
 behind.ui_layout_dirty=true;behind.ui_dirty=true;behind.render(false,true);auto lower=behind.snapshot();behind.operate({{"op","create"},{"id",2},{"width",40},{"height",30},{"color",164}});
 behind.ui_layout_dirty=true;behind.ui_dirty=true;behind.render(false,true);auto upper=behind.snapshot();auto&top=behind.windows.at(2);behind.marker_begin(2,3,initial,228);behind.marker_resize(top,true);behind.render(false,true);
 auto bs=reinterpret_cast<const uint8_t*>(behind.mock_memory[BO_SCENE0+behind.active].data()+header);
 for(unsigned t:top.footprint)assert(behind.display_metadata[256+t]==behind.colors.at(228)*1024);
 Window small{};small.width=8;small.height=6;small.padding=top.padding;behind.marker_resize(small);behind.render_marker();
 auto shrink=behind.snapshot()["tile_hashes"],expected=lower["tile_hashes"];
 for(unsigned t:top.footprint)if(!std::binary_search(behind.move_marker.rim.begin(),behind.move_marker.rim.end(),t))assert(shrink[t]==expected[t]);
 behind.marker_cancel();behind.render(false,true);assert(behind.snapshot()==upper);
 std::cout<<"PASS_MOVE_MARKER: whole move tile, filled blank resize, hidden lower-content reveal, alternating banks, cancel, occlusion, immutable shared S payload\n";
}
