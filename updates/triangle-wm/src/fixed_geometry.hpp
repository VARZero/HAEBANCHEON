#pragma once
#include "fixed_math.hpp"
#include "tile_geometry.hpp"
#include "primitive_init.h"
using Fx=fixed::Number;
using FxV=fixed::Vec;
inline uint64_t magnitude(int64_t v){return fixed::mag(v);}
struct FxGeometry {
 struct Projection {
  unsigned w,h;std::array<FxV,3> ax;Fx tx,ty;
  Projection(unsigned width,unsigned height,Fx y,Fx p,Fx roll,Fx fh,Fx fv):w(width),h(height),ax(fixed::axes(y,p,roll)),tx(2*fixed::tan(fixed::rad(fh/2))),ty(2*fixed::tan(fixed::rad(fv/2))){}
 };
 std::array<int32_t,180> camera(Fx y=0,Fx p=0)const{
  std::array<int32_t,180> out{};
  if(y==0&&p==0){std::copy_n(initial_camera,180,out.begin());return out;}
  auto a=fixed::axes(y,p);FxV dx=a[1]*(2*fixed::tan(fixed::rad(24))/1280),dy=a[2]*(-2*fixed::tan(fixed::rad(18))/720);
  std::array<FxV,3> vectors{a[0]-dx*Fx::ratio(1279,2)-dy*Fx::ratio(719,2),dx,dy};
  for(int f=0;f<20;f++)for(int r=0;r<3;r++)for(int c=0;c<3;c++){
   int64_t sum=0;for(int k=0;k<3;k++)sum+=int64_t(camera_inverse[f][r][k])*vectors[c][k].raw;
   out[f*9+r*3+c]=int32_t(sum>=0?(sum+(1LL<<25))>>26:-((-sum+(1LL<<25))>>26));
  }
  return out;
 }
};
struct FxProjection {
 std::array<FxV,3> basis;unsigned w,h;
 int32_t clip[5][3]{};
 explicit FxProjection(const FxGeometry::Projection&p):w(p.w),h(p.h){
  basis[0]=(p.ax[1]*(Fx(1)/p.tx)+p.ax[0]*Fx::ratio(1,2))*(p.w-1);
  basis[1]=(p.ax[2]*(-Fx(1)/p.ty)+p.ax[0]*Fx::ratio(1,2))*(p.h-1);basis[2]=p.ax[0];
  std::array<FxV,5> planes{basis[2],basis[0],basis[1],basis[2]*(w-1)-basis[0],basis[2]*(h-1)-basis[1]};
  for(unsigned j=0;j<5;j++){
   uint64_t largest=0;for(auto v:planes[j])largest=std::max(largest,fixed::mag(v.raw));
   if(largest)for(unsigned k=0;k<3;k++)clip[j][k]=int32_t(planes[j][k].raw*8192/int64_t(largest));
  }
 }
 bool candidate(unsigned tile)const{
  for(unsigned p=0;p<5;p++){
   int64_t upper=INT64_MIN;
   for(auto&v:tile_vertex[tile])upper=std::max(upper,int64_t(clip[p][0])*v[0]+int64_t(clip[p][1])*v[1]+int64_t(clip[p][2])*v[2]);
   if(upper < -65536)return false;
  }
  return true;
 }
};
inline std::pair<boayo::Descriptor,bool> fx_descriptor(unsigned id,const FxProjection&p){
 int64_t c[3][4];uint64_t bound=0;
 for(int j=0;j<3;j++){
  for(int k=0;k<4;k++){
   FxV coeff{};for(int a=0;a<3;a++)coeff[a]=Fx::bits(tile_coefficients_q44[id][k][a]>>20);
   c[j][k]=fixed::dot(coeff,p.basis[j]).raw;
  }
  bound=std::max(bound,fixed::mag(c[j][0])+31*(fixed::mag(c[j][1])+fixed::mag(c[j][2]))+fixed::mag(c[j][3]));
 }
 int bits=bound?64-__builtin_clzll(bound):1;
 int scale=std::min(22,53-bits),shift=24-scale;
 boayo::Descriptor d{};d[1]=id*256;d[2]=p.w;d[3]=p.h;d[4]=p.w;d[5]=1;
 for(int j=0;j<3;j++)for(int k=0;k<4;k++){
  uint64_t v=fixed::mag(c[j][k]);v=(v+(uint64_t(1)<<(shift-1)))>>shift;
  d[6+j*4+k]=uint32_t(int32_t(c[j][k]<0?-int64_t(v):int64_t(v)));
 }
 bool visible=true;
 for(int j=0;j<5;j++){
  int64_t v[4];for(int k=0;k<4;k++){
   int64_t u=int32_t(d[6+k]),y=int32_t(d[10+k]),z=int32_t(d[14+k]);
   v[k]=j==0?z:j==1?u:j==2?y:j==3?z*(p.w-1)-u:z*(p.h-1)-y;
  }
  int64_t upper=std::max({v[0],v[0]+31*v[1],v[0]+31*(v[1]+v[2])})+std::max(int64_t(0),v[3]);
  visible&=j==0?upper>0:upper>=0;
 }
 return {d,visible};
}
inline std::pair<boayo::Descriptor,bool> fast_descriptor(const FxGeometry&,unsigned id,const FxGeometry::Projection&,const FxProjection&t,unsigned&){return fx_descriptor(id,t);}
inline std::array<int32_t,180> fx_camera(Fx y=0,Fx p=0){return FxGeometry().camera(y,p);}
