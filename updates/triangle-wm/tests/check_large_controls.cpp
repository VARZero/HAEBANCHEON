#define TRIANGLE_ENGINE_ONLY
#ifndef MARKER_ENGINE_HEADER
#define MARKER_ENGINE_HEADER "../src/triangle_wm.cpp"
#endif
#include MARKER_ENGINE_HEADER
#include <cassert>
#include <iostream>
int main(){
 Engine e(true);e.reserve_window_colors();e.ui_enabled=true;e.pointer.visible=false;
 e.operate({{"op","create"},{"id",1},{"width",28},{"height",20},{"padding",3},{"color",36}});
 e.ui_layout_dirty=true;e.ui_dirty=true;e.render(false,true);auto controls=e.ui_controls;assert(controls.size()==3);
 for(const auto&c:controls){
  e.ui_controls={c};e.ui_layout_dirty=false;e.ui_mask_dirty=true;e.ui_button_cache.clear();e.ui_compose(e.metadata,unsigned(e.active)^1);
  int xmin=9999,ymin=9999,xmax=-1,ymax=-1;unsigned black=0,white=0;
  for(auto&[t,paint]:e.ui_paints){UiSamples samples;const auto&d=e.ui_descriptor(t);
   for(unsigned cell=0;cell<1024;cell++){
    black+=bool(paint.mask[0][cell/32]&(1u<<(cell%32)));
    if(!(paint.mask[1][cell/32]&(1u<<(cell%32))))continue;
    unsigned row=0;while((row+1)*(row+1)<=cell)row++;unsigned col=(cell-row*row)/2,inv=(cell-row*row)%2;
    auto v=samples.get(d,cell,row,col,inv);assert(v!=UINT32_MAX);int x=UiSamples::x(v),y=UiSamples::y(v);xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);white++;
   }
  }
  assert(black&&white&&xmax-xmin>=18&&ymax-ymin>=18);assert(e.ui_hit(c.x,c.y).kind==c.kind);
  if(c.kind==1){
   auto count_ink=[&](unsigned color){unsigned count=0;for(auto&[tile,paint]:e.ui_paints)for(auto word:paint.mask[color])count+=__builtin_popcount(word);return count;};
   assert(count_ink(1)>0&&count_ink(2)==0);
   // Keep the warm static mask cache: changing keyboard focus must select
   // the focused variant, rather than replaying the cached idle glyph.
   e.keyboard_close_window=1;e.ui_mask_dirty=true;e.ui_compose(e.metadata,unsigned(e.active)^1);assert(count_ink(2)>0&&count_ink(1)==0);
   auto focused=e.ui_paints;e.ui_mask_dirty=true;e.ui_compose(e.metadata,unsigned(e.active)^1);assert(count_ink(2)>0);
   e.ui_button_cache.clear();e.ui_mask_dirty=true;e.ui_compose(e.metadata,unsigned(e.active)^1);
   assert(focused.size()==e.ui_paints.size());for(auto&[tile,paint]:focused)assert(paint.mask==e.ui_paints.at(tile).mask);
   e.keyboard_close_window=0;e.ui_mask_dirty=true;e.ui_compose(e.metadata,unsigned(e.active)^1);assert(count_ink(1)>0&&count_ink(2)==0);
  }
  std::cout<<"CONTROL "<<c.kind<<" ink_extent="<<xmax-xmin<<'x'<<ymax-ymin<<" white_cells="<<white<<" plate_cells="<<black<<'\n';
 }
 // Hiding controls must clear warm paint masks and remove their hit regions.
 e.windows.at(1).controls_enabled=false;e.ui_layout_dirty=true;e.ui_mask_dirty=true;e.ui_dirty=true;e.render(false,true);
 assert(e.ui_controls.empty()&&e.ui_paints.empty());
 for(const auto&c:controls)assert(e.ui_hit(c.x,c.y).kind==0);
 e.windows.at(1).controls_enabled=true;e.ui_layout_dirty=true;e.ui_mask_dirty=true;e.ui_dirty=true;e.render(false,true);
 assert(e.ui_controls.size()==3);
 std::cout<<"PASS_LARGE_CONTROLS: real projected glyph extents, persistent black plates, white ink, hit regions and hidden-mask restoration\n";
}
