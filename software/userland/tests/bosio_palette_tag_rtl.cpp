#include "Vbosio_ddr_scene_cache.h"
#include "verilated.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <vector>
static void check(bool condition,const char *text){if(!condition)throw std::runtime_error(text);}
struct Test {
 Vbosio_ddr_scene_cache d;
 std::vector<uint32_t> memory[2];std::deque<uint32_t> expected;
 uint64_t cycles=0;unsigned address=0,beat=0,length=0,pixels=0,test_index=0;bool reading=false;
 static uint32_t rgb(unsigned i){return (i<<16)|((255-i)<<8)|((i*71)&255);}
 Test(){
  for(unsigned bank=0;bank<2;++bank){auto &m=memory[bank];m.assign(4736,0);
   for(unsigned i=0;i<256;++i)m[i]=rgb(i)|(bank?((i^0xa5u)<<24):0);
   for(unsigned i=256;i<4476;++i)m[i]=0xffffffffu;m[256]=0;
   for(unsigned i=0;i<256;++i)m[4476+i]=(4*i)|((4*i+1)<<8)|((4*i+2)<<16)|((4*i+3)<<24);
  }
  d.rst_n=0;d.request=0;d.patch_request=0;d.frame_start=0;d.sample_valid=0;d.output_ready=1;d.rresp=0;
  for(int i=0;i<12;++i)tick();d.rst_n=1;for(int i=0;i<12;++i)tick();
 }
 uint32_t read_word(){unsigned bank=address>=0x200000?1:0,base=bank?0x200000:0x100000;
  check(address>=base,"AXI address");unsigned index=(address-base)/4+beat;
  check(index<memory[bank].size(),"AXI bounds");return memory[bank][index];}
 void tick(){
  d.clk=0;d.arready=!reading;d.rvalid=reading;d.rlast=reading&&beat==length;d.rdata=reading?read_word():0;d.eval();
  bool ar=d.arvalid&&d.arready,r=d.rvalid&&d.rready;
  if(d.sample_valid&&d.sample_ready)expected.push_back(rgb(test_index));
  if(d.pixel_valid){check(!expected.empty(),"unsolicited pixel");
   if(d.pixel!=expected.front()){std::fprintf(stderr,"got=%06x expected=%06x\n",d.pixel,expected.front());throw std::runtime_error("palette changed RGB");}
   expected.pop_front();++pixels;}
  unsigned next_address=d.araddr,next_length=d.arlen;
  d.clk=1;d.eval();++cycles;
  if(r){if(beat==length)reading=false;else ++beat;}
  if(ar){check(!reading,"AXI overlap");reading=true;address=next_address;length=next_length;beat=0;}
 }
 void scene(unsigned bank,unsigned index){
  test_index=index;memory[bank][4476]=index*0x01010101u;
  d.base=bank?0x200000:0x100000;d.word_count=4736;d.request=1;tick();d.request=0;
  unsigned guard=0;while(!d.pending){check(++guard<200000,"metadata load timeout");tick();}
  check(d.frame_ready,"scene not idle");d.frame_start=1;tick();d.frame_start=0;tick();
  check(d.scene_valid&&!d.error&&d.active_scene_base==(bank?0x200000u:0x100000u),"scene commit");
  unsigned first=pixels,sent=0;guard=0;
  while(sent<1 || pixels-first<1){
   check(++guard<200000,"pixel timeout");d.sample_valid=sent<1&&d.sample_ready;d.directory_addr=0;d.cell_addr=0;
   if(d.sample_valid)++sent;tick();
  }
  d.sample_valid=0;for(int i=0;i<20;++i)tick();check(expected.empty()&&!d.error,"pixel drain");
 }
};
int main(int argc,char **argv){try{Verilated::commandArgs(argc,argv);Test test;for(unsigned i=0;i<256;++i){test.scene(0,i);test.scene(1,i);}
 std::printf("PASS_BS25_PALETTE_UPPER_BYTE_IGNORED palettes=256 pixels=%u cycles=%llu\n",test.pixels,(unsigned long long)test.cycles);return 0;
 }catch(const std::exception &e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
