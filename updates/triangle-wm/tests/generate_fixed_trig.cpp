#include "common.hpp"
#include "fixed_math.hpp"
#include <cstdio>
#include <cassert>
int main(){
 std::puts("// Exact host evaluation of the original fixed-point functions.\n#pragma once\nstatic constexpr int32_t degree_sincos_raw[1441][2]={");
 for(int i=-720;i<=720;i++){
  auto q=fixed::sincos(fixed::rad(fixed::Number::bits(int64_t(i)*4194304)));
  for(auto n:q)assert(n.raw>=INT32_MIN&&n.raw<=INT32_MAX);
  std::printf("{%d,%d},\n",int(q[0].raw),int(q[1].raw));
 }
 std::puts("};\nstatic constexpr int32_t degree_tan_raw[513]={");
 for(int i=0;i<=512;i++){
  auto q=fixed::tan(fixed::rad(fixed::Number::bits(int64_t(i)*2097152)));assert(q.raw>=INT32_MIN&&q.raw<=INT32_MAX);
  std::printf("%d,\n",int(q.raw));
 }
 std::puts("};");
}
