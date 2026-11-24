#include "common.hpp"
#include "fixed_math.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using fixed::Number;uint64_t count=0;
 auto check=[&](int64_t n,int64_t d){
  auto a=Number::bits(n),b=Number::bits(d);
  assert(fixed::plane_q13(a,b)==int32_t(fixed::lround(a/b*8192)));++count;
 };
 for(int64_t d=1;d<512;d++)for(int64_t n=0;n<=d;n++){check(n,d);check(-n,d);}
 for(int64_t n=1;n<16384;n+=2){check(n,16384);check(-n,16384);}
 uint32_t seed=0x31957u;
 for(unsigned i=0;i<1000000;i++){
  seed=seed*1664525u+1013904223u;uint32_t d=(seed&0x7fffffffu)|1;
  seed=seed*1664525u+1013904223u;uint32_t n=seed%(uint64_t(d)+1);
  check(n,d);check(-int64_t(n),d);
 }
 for(int64_t d:{int64_t(1)<<31,int64_t(1)<<40})for(int64_t n:{int64_t(0),int64_t(1),d/2,d,d+1}){check(n,d);check(-n,d);check(n,-d);}
 bool failed=false;try{fixed::plane_q13(Number(1),Number(0));}catch(const std::domain_error&){failed=true;}assert(failed);
 std::printf("PASS_PLANE_Q13: %llu exact comparisons including ties, signs, large fallback and zero denominator\n",(unsigned long long)count);
}
