#include "../software/boayo/native/src/rgb_fill.hpp"
#include <cassert>
#include <iostream>
using namespace boayo;
int main() {
  alignas(4096) uint8_t memory[16384];
  unsigned calls=0;
  const std::array<uint8_t,3> color{3,5,8};
  for(bool available:{false,true}) {
    RgbFillScope scope({&calls,[](void *v,uint8_t *p,size_t n,const std::array<uint8_t,3> &c){
      ++*static_cast<unsigned *>(v);
      for(size_t i=0;i<n;++i)p[i]=c[i%3];return true;
    }});
    if(!available)rgb_fill_hook={};
    for(unsigned off=0;off<8;++off)for(unsigned n:{0,1,2,3,4,4095,4096,4097,4100,8192}) {
      std::fill(std::begin(memory),std::end(memory),0xa5);
      unsigned before=calls;fill_rgb(memory+off,n,color);
      assert(calls-before==unsigned(available && n>=4096 && !(off&3) && !(n&3)));
      for(unsigned i=0;i<n;i++)assert(memory[off+i]==color[i%3]);
      for(unsigned i=0;i<off;i++)assert(memory[i]==0xa5);
      assert(memory[off+n]==0xa5);
    }
  }
  assert(!rgb_fill_hook.fill);
  { RgbFillScope scope({nullptr,[](void *,uint8_t *,size_t,const std::array<uint8_t,3>&)->bool{
      throw std::runtime_error("DMA fault");
    }});
    std::fill(std::begin(memory),std::end(memory),0xa5);
    bool failed=false;try{fill_rgb(memory,4096,color);}catch(...){failed=true;}
    assert(failed && memory[0]==0xa5); // No silent CPU retry after a DMA error.
  }
  std::cout<<"PASS RGB dispatch thresholds alignment tails colors canaries scope error propagation\n";
}
