#include "texture_pool.hpp"
#include "apps.hpp"
#include <random>
#include <iostream>
using namespace boayo;
static std::shared_ptr<const Texture> texture(unsigned w,unsigned h,unsigned seed){
 auto*p=(uint32_t*)mmap(nullptr,w*h*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(p!=MAP_FAILED,"test mmap");
 for(unsigned i=0;i<w*h;i++)p[i]=(seed+i*0x010307u)&0xffffff;return std::make_shared<Texture>(p,w*h);
}
static int window(WM&w,unsigned x,unsigned y){return w.dispatch({{"op","create_window"},{"app","test"},{"args",{{"title","test"},{"surface_width",x},{"surface_height",y},{"decorated",false}}}})["window_id"];}
static Surface recipe(std::shared_ptr<const Texture>b,std::vector<Texture::Patch>p,unsigned w,unsigned h){return Surface(w,h,{},std::make_shared<Texture>(b,std::move(p)));}
static void packed(Window&w,Layer&l){if(w.prepared)return;l.source.resize(w.surface.size()/3);for(size_t i=0;i<l.source.size();i++)l.source[i]=uint32_t(w.surface[i*3])<<16|uint32_t(w.surface[i*3+1])<<8|w.surface[i*3+2];}
static void check(TexturePool&pool,MockDevice&dev,WM&wm,int id,Layer&l){
 auto&w=wm.window(id);packed(w,l);auto&s=pool.ensure(id,w,l);Bytes rgb;
 if(w.prepared)materialize_rgb(rgb,*w.prepared);else rgb=w.surface;
 for(unsigned i=0;i<s.words;i++){uint32_t ref=uint32_t(rgb[i*3])<<16|uint32_t(rgb[i*3+1])<<8|rgb[i*3+2];require(dev.regions[0][s.offset+i]==ref,"resident pixel mismatch");}
 w.dirty={};w.dirty_regions.clear();
}
int main(){
 WM wm;MockDevice dev;TexturePool pool(dev,8192);Layer l;
 auto base=texture(64,64,7),a=texture(8,8,0x123456),b=texture(8,8,0xabcdef);int id=window(wm,64,64);
 publish(wm,id,recipe(base,{{a,0,10+20*64,8,8,8,64}},64,64),true);check(pool,dev,wm,id,l);
 auto before=pool.upload_bytes;auto revision=wm.window(id).revision;
 publish(wm,id,recipe(base,{{a,0,10+20*64,8,8,8,64}},64,64));require(wm.window(id).revision==revision,"equal recipe must not dirty");check(pool,dev,wm,id,l);require(pool.upload_bytes==before,"unchanged upload");
 publish(wm,id,recipe(base,{{b,0,10+20*64,8,8,8,64}},64,64));require(wm.window(id).dirty==std::array<int,4>{10,20,8,8},"widget dirty rectangle");check(pool,dev,wm,id,l);
 require(pool.upload_bytes-before==8*8*4,"small widget must not upload base");
 publish(wm,id,recipe(base,{{a,0,12+22*64,8,8,8,64},{b,0,15+25*64,8,8,8,64}},64,64));check(pool,dev,wm,id,l);
 publish(wm,id,recipe(base,{{a,0,12+22*64,8,8,8,64}},64,64));check(pool,dev,wm,id,l);
 publish(wm,id,recipe(base,{},64,64));check(pool,dev,wm,id,l);
 // Reordered/overlapping immutable patches, including skipped compositions.
 std::mt19937 recipes(719);
 for(int k=0;k<100;k++){
  std::vector<Texture::Patch> pieces;
  for(unsigned n=recipes()%6;n;n--){unsigned x=recipes()%57,y=recipes()%57;pieces.push_back({recipes()%2?a:b,0,x+y*64,8,8,8,64});}
  publish(wm,id,recipe(base,std::move(pieces),64,64));pool.note(id,wm.window(id));
  if(k%3==0){wm.window(id).dirty={};wm.window(id).dirty_regions.clear();}
  else check(pool,dev,wm,id,l);
 }
 check(pool,dev,wm,id,l);
 // CPU widget over a prepared image, then incremental raw CPU updates.
 before=pool.upload_bytes;Surface cpu(3,2,{231,17,99});publish_region(wm,id,cpu,9,11);check(pool,dev,wm,id,l);
 require(pool.upload_bytes-before<1024,"CPU patch must not upload whole image");
 std::mt19937 rng(123);for(int k=0;k<60;k++){int x=rng()%56,y=rng()%56;Surface part(1+rng()%8,1+rng()%8,{uint8_t(rng()),uint8_t(rng()),uint8_t(rng())});publish_region(wm,id,part,x,y);check(pool,dev,wm,id,l);}
 // A completely covered window can consume revisions without using its texture.
 for(int k=0;k<4;k++){publish_region(wm,id,Surface(2,2,{uint8_t(k*41),18,219}),k*15,k*15);pool.note(id,wm.window(id));wm.window(id).dirty={};wm.window(id).dirty_regions.clear();}
 check(pool,dev,wm,id,l);
 // Two windows retain distinct addresses; only a third forces bounded eviction.
 int id2=window(wm,64,64);auto other=texture(64,64,99);publish(wm,id2,Surface(64,64,{},other),true);Layer l2;check(pool,dev,wm,id2,l2);
 require(pool.slots.at(id).offset!=pool.slots.at(id2).offset,"overlapping allocations");before=pool.upload_bytes;check(pool,dev,wm,id,l);check(pool,dev,wm,id2,l2);require(pool.upload_bytes==before,"window switch retransmitted texture");
 int id3=window(wm,64,64);publish(wm,id3,Surface(64,64,{},texture(64,64,201)),true);Layer l3;check(pool,dev,wm,id3,l3);require(pool.evictions==1,"LRU eviction");check(pool,dev,wm,id,l);require(pool.evictions==2,"evicted image reload");
 wm.dispatch({{"op","destroy_window"},{"app","test"},{"args",{{"window_id",id3}}}});pool.prune(wm);require(!pool.slots.count(id3),"closed window released");
 // Failed partial upload is never reused as a completed texture.
 dev.fail_after=0;publish_region(wm,id,Surface(2,2,{1,2,3}),0,0);packed(wm.window(id),l);bool failed=false;try{pool.ensure(id,wm.window(id),l);}catch(...){failed=true;}require(failed && !pool.slots.count(id),"failed texture must be invalidated");
 std::cout<<"PASS dirty texture pool: pixel oracle, 100 random recipes, 60 CPU edits, skipped updates, multiwindow reuse, eviction, failure\n";
 std::cout<<pool.status().dump()<<"\n";
}
