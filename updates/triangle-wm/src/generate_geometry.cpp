#include "geometry.hpp"
#include <cstdio>
using namespace boayo;
int main(){Geometry g;puts("// Generated from the pinned BOSIO spherical geometry. Q13 vertices.\n#pragma once\n#include <cstdint>\nstatic constexpr int16_t tile_vertex[4220][3][3]={");
 for(auto &t:g.triangles){printf("{");for(auto &v:t)printf("{%d,%d,%d},",int(std::lround(v[0]*8192)),int(std::lround(v[1]*8192)),int(std::lround(v[2]*8192)));puts("},");}puts("};\nstatic constexpr int16_t group_bounds[132][6]={");
 for(unsigned i=0;i<tiles;i+=32){int a[6]={32767,32767,32767,-32767,-32767,-32767};for(unsigned j=i;j<std::min(i+32,tiles);j++)for(auto &v:g.triangles[j])for(int k=0;k<3;k++){int q=std::lround(v[k]*8192);a[k]=std::min(a[k],q);a[k+3]=std::max(a[k+3],q);}printf("{%d,%d,%d,%d,%d,%d},\n",a[0],a[1],a[2],a[3],a[4],a[5]);}puts("};\nstatic constexpr int32_t camera_inverse[20][3][3]={");
 for(auto&f:g.faces){double inv=1/dot(f[0],cross(f[1],f[2]));std::array<V,3> rows{cross(f[1],f[2])*inv,cross(f[2],f[0])*inv,cross(f[0],f[1])*inv};printf("{");for(auto&r:rows)printf("{%d,%d,%d},",int(std::lround(r[0]*67108864)),int(std::lround(r[1]*67108864)),int(std::lround(r[2]*67108864)));puts("},");}puts("};");
 puts("static constexpr double tile_coefficients[4220][4][3]={");
 for(const auto&t:g.triangles){std::array<V,4> c{t[0]*94+t[1]+t[2],(t[1]-t[0])*3,(t[2]-t[1])*3,t[0]-t[1]*2+t[2]};printf("{");for(const auto&v:c)printf("{%a,%a,%a},",v[0],v[1],v[2]);puts("},");}puts("};");
 puts("static constexpr int64_t tile_coefficients_q44[4220][4][3]={");
 for(const auto&t:g.triangles){std::array<V,4> c{t[0]*94+t[1]+t[2],(t[1]-t[0])*3,(t[2]-t[1])*3,t[0]-t[1]*2+t[2]};printf("{");for(const auto&v:c)printf("{%lldLL,%lldLL,%lldLL},",(long long)std::llround(v[0]*0x1p44),(long long)std::llround(v[1]*0x1p44),(long long)std::llround(v[2]*0x1p44));puts("},");}puts("};");}
