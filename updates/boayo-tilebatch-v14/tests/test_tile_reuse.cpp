#include "texture_pool.hpp"
#include "apps.hpp"
#include <iostream>
using namespace boayo;
static auto image(unsigned w,unsigned h,unsigned seed){auto*p=(uint32_t*)mmap(nullptr,w*h*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);require(p!=MAP_FAILED,"mmap");for(unsigned i=0;i<w*h;++i)p[i]=(seed+i*193)&0xffffff;return std::make_shared<const Texture>(p,w*h);}
static int create(WM&w,unsigned x,unsigned y){return w.dispatch({{"op","create_window"},{"app","test"},{"args",{{"title","reuse"},{"surface_width",x},{"surface_height",y},{"decorated",false}}}})["window_id"];}
static void verify(TexturePool&p,MockDevice&d,WM&w,int id,Layer&l){auto &win=w.window(id);auto&s=p.ensure(id,win,l);Bytes reference;materialize_rgb(reference,*win.prepared);for(unsigned i=0;i<s.words;++i){uint32_t q=uint32_t(reference[i*3])<<16|uint32_t(reference[i*3+1])<<8|reference[i*3+2];require(d.regions[0][s.offset+i]==q,"pixel oracle mismatch");}win.dirty={};win.dirty_regions.clear();}
int main(){WM wm;MockDevice dev;TexturePool pool(dev);Layer l;auto base=image(256,128,3);std::vector<std::shared_ptr<const Texture>> digits;for(unsigned i=0;i<10;++i)digits.push_back(image(16,24,i*1000));int id=create(wm,256,128);
 uint64_t before=0,calls=0,batches=0;
 for(unsigned frame=0;frame<50;++frame){std::vector<Texture::Patch> patches;for(unsigned i=0;i<8;++i)patches.push_back({digits[(frame+i)%10],0,10+i*28+40*256,16,24,16,256});publish(wm,id,Surface(256,128,{},std::make_shared<Texture>(base,patches)),frame==0);verify(pool,dev,wm,id,l);
  if(frame==9){before=pool.upload_bytes;calls=pool.upload_calls;dev.trace.clear();}}
 for(auto &e:dev.trace)if(e["op"]=="batch")++batches;
 J report={{"result","PASS_TILE_REUSE_PIXELS"},{"warm_frames",40},{"warm_upload_bytes",pool.upload_bytes-before},{"warm_upload_calls",pool.upload_calls-calls},{"warm_batch_calls",batches},{"pool",pool.status()}};
 auto last=pool.upload_bytes;verify(pool,dev,wm,id,l);require(pool.upload_bytes==last,"unchanged window upload");
 auto big=image(512,512,33),piece=image(16,16,999);int diagonal=create(wm,512,512);Layer other;publish(wm,diagonal,Surface(512,512,{},big),true);verify(pool,dev,wm,diagonal,other);
 auto bytes=pool.upload_bytes;std::vector<Texture::Patch> patches;for(unsigned n=0;n<32;++n)patches.push_back({piece,0,n*16+n*16*512,16,16,16,512});publish(wm,diagonal,Surface(512,512,{},std::make_shared<Texture>(big,patches)));verify(pool,dev,wm,diagonal,other);report["diagonal_upload_bytes"]=pool.upload_bytes-bytes;
 std::cout<<report.dump()<<std::endl;
}
