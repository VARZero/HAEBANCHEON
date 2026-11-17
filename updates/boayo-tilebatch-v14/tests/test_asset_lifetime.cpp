#include "texture_pool.hpp"
#include "apps.hpp"
#include <iostream>
using namespace boayo;
static auto image(unsigned w,unsigned h,unsigned seed){auto*p=(uint32_t*)mmap(nullptr,w*h*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(p!=MAP_FAILED,"mmap");for(unsigned i=0;i<w*h;++i)p[i]=(seed+i*193)&0xffffff;return std::make_shared<const Texture>(p,w*h);}
static int create(WM&w,unsigned x,unsigned y){return w.dispatch({{"op","create_window"},{"app","test"},{"args",{{"title","reuse"},{"surface_width",x},{"surface_height",y},{"decorated",false}}}})["window_id"];}
static void verify(TexturePool&p,MockDevice&d,WM&w,int id,Layer&l){auto &win=w.window(id);auto&s=p.ensure(id,win,l);Bytes reference;materialize_rgb(reference,*win.prepared);for(unsigned i=0;i<s.words;++i){uint32_t q=uint32_t(reference[i*3])<<16|uint32_t(reference[i*3+1])<<8|reference[i*3+2];require(d.regions[0][s.offset+i]==q,"pixel oracle mismatch");}win.dirty={};win.dirty_regions.clear();}
int main(){
 for(int failure=-1;failure<12;++failure){WM wm;MockDevice dev;TexturePool pool(dev,65536);Layer l;auto base=image(256,128,9);int id=create(wm,256,128);std::vector<std::shared_ptr<const Texture>> parts;for(unsigned i=0;i<9;++i)parts.push_back(image(64,64,i*8000));
  publish(wm,id,Surface(256,128,{},base),true);verify(pool,dev,wm,id,l);
  if(failure>=0)dev.fail_after=failure;
  bool caught=false;
  try{for(unsigned f=0;f<20;++f){std::vector<Texture::Patch> patches;for(unsigned i=0;i<9;++i)patches.push_back({parts[(f+i)%9],0,(i*21)%193+((i*11)%65)*256,64,64,64,256});publish(wm,id,Surface(256,128,{},std::make_shared<Texture>(base,patches)));verify(pool,dev,wm,id,l);}}
  catch(const std::runtime_error&){caught=true;if(failure<0)throw;require(!pool.slots.count(id),"failed texture was retained");}
  require(failure<0?!caught:caught,"failure injection did not execute");
 }
 std::cout<<"PASS_ASSET_EVICTION_ORDER_OVERLAPS_AND_12_FAILURE_POSITIONS\n";
}
