// Compare cached/delta composition against an independent per-cell renderer.
#define TRIANGLE_ENGINE_ONLY
#include "../src/triangle_wm.cpp"
#include <random>
int main(){
 Engine e(true);for(unsigned c:{84u,164u,109u,38u,228u,255u})e.solid(c);
 e.ui_enabled=true;e.ui_layout_dirty=false;e.ui_mask_dirty=false;e.pointer.visible=false;
 Words base=e.metadata;const unsigned source_slot=64;
 for(unsigned bank=0;bank<2;bank++){
  auto*src=reinterpret_cast<uint8_t*>(e.mock_memory[BO_SCENE0+bank].data()+header+source_slot*256);
  for(unsigned i=0;i<1024;i++)src[i]=(i*17+i/7)&255;
 }
 for(unsigned t=0;t<600;t++){base[256+t]=t%3==0?UINT32_MAX:t%3==1?e.colors.at(84)*1024:source_slot*1024;e.planned_version[t]=t%3==2?1:0;}
 unsigned frame=0;uint64_t deltas=0,restores=0,rebuilds=0;
 auto check=[&]{
  unsigned bank=frame++%2;e.ui_compose(base,bank);
  for(unsigned t=0;t<tiles;t++){
   auto paint=e.ui_paints.find(t);
   if(paint==e.ui_paints.end()){require(e.ui_metadata[256+t]==base[256+t],"inactive overlay leaked into metadata");continue;}
   auto*memory=reinterpret_cast<const uint8_t*>(e.mock_memory[BO_SCENE0+bank].data()+header);
   for(unsigned i=0;i<1024;i++){
    unsigned expected=base[256+t]==UINT32_MAX?0:memory[base[256+t]+i];
    for(unsigned c=0;c<4;c++)if(paint->second.mask[c][i/32]&(1u<<(i%32)))expected=c==0?0:c==1?255:c==2?228:e.preview.color;
    require(memory[e.ui_metadata[256+t]+i]==expected,"delta differs from full per-cell composition");
   }
  }
  deltas+=e.ui_incremental;restores+=e.ui_restored_cells;rebuilds+=e.ui_rebuilt;e.active=bank;
 };
 auto set=[&](unsigned cell,unsigned c){e.ui_paints.clear();for(unsigned t=0;t<3;t++)e.ui_paints[t].mask[c][cell/32]=1u<<(cell%32);};
 set(1,3);check();check();check();require(!e.ui_written_cells&&!e.ui_copy_bytes&&e.ui_reused==3,"warm masks wrote pixels");
 set(2,3);check();require(e.ui_incremental==3&&e.ui_rebuilt==0&&e.ui_restored_cells==3&&e.ui_written_cells==6,"single-cell motion not sparse");
 require(e.ui_copy_bytes==1024,"pattern restore must stage one source tile; solids must not copy");
 check();
 // Same visible red, but ownership changes from splash to pressed-pointer ink.
 set(2,2);check();require(!e.ui_written_cells&&!e.ui_copy_bytes,"same-color class change repainted");
 e.preview.color=255;set(2,3);check();check();
 // New background version at the same address must discard the old cached base.
 for(unsigned bank=0;bank<2;bank++)std::fill_n(e.mock_memory[BO_SCENE0+bank].data()+header+source_slot*256,256,0x26262626u);
 e.planned_version[2]=2;check();require(e.ui_rebuilt==1,"content revision did not invalidate base");check();
 base[257]=e.colors.at(164)*1024;check();require(e.ui_rebuilt==1,"solid background change did not invalidate");check();
 std::mt19937 random(0x51ad);
 // More than 256 distinct tiles force slot eviction. 100 simultaneous overlays
 // also exercise ordered scratch reuse across BO_BATCH_MAX boundaries.
 for(unsigned round=0;round<70;round++){
  e.ui_paints.clear();e.preview.color=round%3?228:255;
  unsigned start=(round/4*37)%450;
  for(unsigned t=start;t<start+100;t++){
   auto&p=e.ui_paints[t];
   for(unsigned k=0;k<32;k++){unsigned cell=random()%1024,c=random()%4;for(auto&m:p.mask)m[cell/32]&=~(1u<<(cell%32));p.mask[c][cell/32]|=1u<<(cell%32);}
  }
  check();require(e.ui_slots.size()<=256,"overlay cache unbounded");
 }
 e.ui_paints.clear();check();check();require(!e.ui_written_cells&&!e.ui_copy_bytes,"cancel needlessly touched payloads");
 require(deltas>100&&restores>100&&rebuilds>256,"did not exercise delta/restore/eviction");
 std::cout<<"PASS_UI_DELTA: exact cell output, sparse writes, both banks, solid/pattern restoration, new content, color overlap, slot eviction, batch boundaries\n";
}
