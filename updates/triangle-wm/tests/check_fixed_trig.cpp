#include "common.hpp"
#include "fixed_math.hpp"
#include <cassert>
#include <cstdio>
int main(){
#ifndef FIXED_TRIG_TABLES
#error Table path must be enabled in this test
#endif
 using fixed::Number;
 for(int i=-720;i<=720;i++){
  auto a=Number::bits(int64_t(i)*4194304);assert(fixed::sincos_degrees(a)==fixed::sincos(fixed::rad(a)));
  a+=Number::bits(1);assert(fixed::sincos_degrees(a)==fixed::sincos(fixed::rad(a)));
 }
 for(int i=0;i<=512;i++){
  auto a=Number::bits(int64_t(i)*2097152);assert(fixed::tan_degrees(a)==fixed::tan(fixed::rad(a)));
  a+=Number::bits(1);assert(fixed::tan_degrees(a)==fixed::tan(fixed::rad(a)));
 }
 for(int i:{-721,721,1000}){auto a=Number::bits(int64_t(i)*4194304);assert(fixed::sincos_degrees(a)==fixed::sincos(fixed::rad(a)));}
 std::puts("PASS_FIXED_TRIG_TABLES: every quarter-degree sine/cosine and eighth-degree tangent exactly matches original integer functions; off-grid/out-of-range fallback unchanged");
}
