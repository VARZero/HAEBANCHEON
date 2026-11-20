#include "geometry.hpp"
#include "tile_geometry.hpp"
using namespace boayo;
#include "fast_projection.hpp"
#include "content.hpp"
#include "window_transform.hpp"
#include "texture_cache.hpp"
#include <random>
#include <iostream>
int main() {
  Geometry g;std::mt19937 random(20261120);unsigned checked=0,fallbacks=0,rejected=0;
  TextureCache cache;cache.capacity=64;
  for(unsigned n=0;n<10000;n++){
    int id=1+random()%12;unsigned words=1+random()%64;
    auto &e=cache.acquire(id,words);require(e.words==words && e.offset+words<=64,"cache bounds");e.valid=true;e.revision=n+1;
    for(auto &[a,x]:cache.entries)for(auto &[b,y]:cache.entries)if(a!=b)require(x.offset+x.words<=y.offset || y.offset+y.words<=x.offset,"cache overlap");
    if(n%17==0)cache.erase(id);
  }
  cache.clear();cache.acquire(1,32);cache.acquire(2,32);cache.acquire(1,32);cache.acquire(3,32);
  require(cache.entries.count(1)&&!cache.entries.count(2)&&cache.entries.count(3),"LRU victim");
  std::cout<<"PASS_TEXTURE_CACHE 10000 allocations and LRU"<<std::endl;
  random.seed(20261120);WindowTransform cached;
  // Host-wide arithmetic reference checks the limb multiplication separately.
  for(unsigned n=0;n<100000;n++){
    int64_t a=int64_t((uint64_t(random())<<20)^random())% (96LL<<44);
    int64_t b=int64_t((uint64_t(random())<<12)^random())% (1LL<<44);
    if(n&1)a=-a;if(n&2)b=-b;
    __int128 product=__int128(a)*b;
    int64_t expected=product<0?-int64_t((-product)>>44):int64_t(product>>44);
    require(multiply_q44(a,b)==expected,"fixed multiply mismatch");
  }
  for(unsigned test=0;test<1024;test++) {
    unsigned w=test%7==0?1:test%5==0?1024:240,h=test%11==0?1:test%3==0?1024:144;
    double yaw=test<20?(int(test)-10)*18:(int(random()%36001)-18000)/100.;
    double pitch=test<20?0:(int(random()%16001)-8000)/100.;
    double width=test%8==0?1:test%8==1?100:1+(random()%9900)/100.;
    double height=test%9==0?1:test%9==1?100:1+(random()%9900)/100.;
    Geometry::Projection p(w,h,yaw,pitch,0,width,height);
    cached.update(yaw,pitch,width,height,2);auto cp=cached.projection(w,h);
    require(cp.ax==p.ax && cp.tx==p.tx && cp.ty==p.ty,"cached transform mismatch");
    FastProjection transform(cp);
    for(unsigned tile=0;tile<tiles;tile++) {
      auto reference=g.descriptor(tile,p),fast=fast_descriptor(g,tile,p,transform,fallbacks);
      if(reference!=fast) {std::cerr<<"mismatch case "<<test<<" tile "<<tile<<std::endl;return 1;}
      if(!transform.candidate(tile)){if(reference.second){std::cerr<<"cull mismatch "<<test<<" tile "<<tile<<std::endl;return 1;}rejected++;}
      checked++;
    }
  }
  Surface rgb;IndexedSurface indexed;rgb.init(240,144,84);indexed.init(240,144,84);
  for(unsigned color=0;color<256;color++) {
    J patch={{"id",1},{"x",5},{"y",5},{"w",150},{"h",60},{"color",color}};rgb.put(patch);indexed.put(patch);
    patch={{"id",2},{"type","text"},{"x",12},{"y",20},{"scale",2},{"text","RGB332 012345"},{"color",255-color}};rgb.put(patch);indexed.put(patch);
    for(size_t i=0;i<rgb.pixels.size();i++) require(rgb.pixels[i]==source_color(indexed.pixels[i]),"indexed painter mismatch");
  }
  std::cout<<"PASS_INDEXED_PAINTER 256 palette colors"<<std::endl;
  std::cout<<"PASS_PROJECTION_EXACT "<<checked<<" fallbacks "<<fallbacks<<" rejected "<<rejected<<std::endl;
}
