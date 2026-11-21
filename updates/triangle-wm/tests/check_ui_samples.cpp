#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
namespace boayo {using Descriptor=std::array<uint32_t,18>;}
#include "../src/ui_samples.hpp"
int main(){
 std::mt19937_64 random(94183);
 for(unsigned i=0;i<1000000;i++){
  uint64_t d=1+(random()%((1ull<<37)-1));
  unsigned pixel=random()%1280;
  uint64_t n=d*pixel+(random()%d);
  assert(UiSamples::quotient(n,d)==n/d);
 }
 // Integer edges, near-integer fractions, and normalization boundaries.
 for(unsigned bit=1;bit<38;bit++)for(int delta=-1;delta<=1;delta++){
  uint64_t d=(1ull<<bit)+delta;
  for(unsigned pixel:{0u,1u,719u,1279u})for(uint64_t rem:{uint64_t(0),uint64_t(1),d-1}){
   uint64_t n=d*pixel+rem;
   assert(UiSamples::quotient(n,d)==n/d);
  }
 }
 std::cout<<"PASS_UI_QUOTIENT: 1000000 random and boundary coordinates match exact division\n";
}
