#include "mask.hpp"
#include "wm.hpp"
#include <chrono>
#include <iostream>
using namespace boayo;
#include "mask_benchmark_data.inc"
int main(){
 J shape=J::array({{48,4,544,428,14}});std::vector<Bytes> fast(16,Bytes(1024)),ref=fast;
 std::array<std::array<uint32_t,32>,16> packed{},refpacked{};std::array<uint8_t,16> kinds{},refkinds{};
 auto measured=[](auto action){auto start=std::chrono::steady_clock::now();action();return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();};
 uint64_t integers=0,precise=0;
 auto runfast=[&](){MaskShape compiled(shape);for(unsigned i=0;i<16;++i){bool full;kinds[i]=prepare_mask(samples[i],compiled,fast[i],packed[i],full);}integers=compiled.integer_samples;precise=compiled.precise_samples;};
 auto fast1=measured(runfast);
 auto original=measured([&](){for(unsigned i=0;i<16;++i){bool any=false,all=true;for(unsigned cell=0;cell<1024;++cell){auto q=coordinates(samples[i],cell);bool v=covered(samples[i],q) && shape_contains(shape,double(q[0])/q[2],double(q[1])/q[2]);ref[i][cell]=v;if(v)refpacked[i][cell/32]|=uint32_t(1)<<(cell%32);any|=v;all&=v;}refkinds[i]=!any?0:all?1:2;}});
 require(fast==ref && packed==refpacked && kinds==refkinds,"RV32 boundary mask mismatch");auto fast2=measured(runfast);require(fast==ref,"RV32 warm mask mismatch");
 for(unsigned i=0;i<16;++i){bool any=false,all=true;for(unsigned c=0;c<1024;++c){bool v=ref[i][c];require(bool((packed[i][c/32]>>(c%32))&1)==v,"RV32 packed mismatch");any|=v;all&=v;}require(kinds[i]==(!any?0:all?1:2),"RV32 kind mismatch");}
 std::cout<<J({{"result","PASS_RV32_MASK_BENCHMARK"},{"tiles",16},{"pixels",16384},{"fast_first_us",fast1},{"reference_us",original},{"fast_second_us",fast2},{"integer_samples",integers},{"precise_samples",precise}}).dump()<<std::endl;
}
