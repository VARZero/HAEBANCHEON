#include "../software/boayo/native/src/backend.hpp"
#include <cassert>
#include <iostream>
using namespace boayo;
static void identical(MockDevice &cpu,MockDevice &dma,Output &a,Output &b)
{
 assert(cpu.active==dma.active && a.active==b.active && a.shadow==b.shadow);
 assert(cpu.regions[BO_SCENE0]==dma.regions[BO_SCENE0]);
 assert(cpu.regions[BO_SCENE1]==dma.regions[BO_SCENE1]);
 assert(a.uploaded_words==2*b.uploaded_words);
 assert(b.dma_copied_words==b.uploaded_words);
}
int main()
{
 Geometry geometry;MockDevice cpu,dma;cpu.dma_enabled=false;
 Output a(cpu,geometry),b(dma,geometry);
 a.initialize_blank();b.initialize_blank();identical(cpu,dma,a,b);
 assert(a.blank_only && b.blank_only && b.uploaded_words==4480);
 Words scene(scene_words,0);
 for(unsigned i=0;i<256;i++)scene[i]=i*0x010101;
 std::fill(scene.begin()+256,scene.begin()+header,0xffffffff);
 scene[256+17]=17*1024;
 for(unsigned i=0;i<256;i++)scene[header+17*256+i]=i*0x030507;
 a.full(scene,true);b.full(scene,true);identical(cpu,dma,a,b);
 for(unsigned step=0;step<32;step++) {
  Words patch(16+16+256,0);
  unsigned tile=(step*97)%tiles;
  patch[0]=0x42505432;patch[1]=256;patch[2]=1;patch[3]=patch.size();
  patch[16]=tile*256;patch[17]=tile;patch[18]=step%3?tile*1024:0xffffffff;
  for(unsigned i=0;i<256;i++)patch[32+i]=(step*1234567)^i;
  a.patch(patch);b.patch(patch);identical(cpu,dma,a,b);
 }
 bool rejected=false;
 try{dma.fill(BO_SCENE0+dma.active,0,4,0);}catch(const std::exception&){rejected=true;}
 assert(rejected);
 assert(dma.fill(BO_SCRATCH,0,1024,0x5a));
 assert(dma.regions[BO_SCRATCH][1023]==0x5a5a5a5a && dma.regions[BO_SCRATCH][1024]==0);
 MockDevice bad;Output broken(bad,geometry);bad.fail_after=3;
 rejected=false;try{broken.initialize_blank();}catch(const std::exception&){rejected=true;}
 assert(rejected && broken.failed && bad.poisoned && bad.active==0);
 rejected=false;try{broken.initialize_blank();}catch(const std::exception&){rejected=true;}
 assert(rejected);
 std::cout<<"PASS CPU-vs-DMA both-bank byte equality: metadata blank, full scene, 32 BPT2 patches; active guard/fill/error quarantine\n";
 std::cout<<"CPU uploaded_words="<<a.uploaded_words<<" DMA uploaded_words="<<b.uploaded_words
          <<" device_copied_words="<<b.dma_copied_words<<"\n";
}
