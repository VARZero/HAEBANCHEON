#pragma once
#include "geometry_math.hpp"
#include <cstring>
// Window transform once; immutable sphere coefficients generated on the host.
struct FastProjection {
  std::array<boayo::V,3> basis;
  int exponent=0; int64_t fixed[3][3];
  int32_t clip[5][3]{};int64_t clip_guard[5]{};
  explicit FastProjection(const boayo::Geometry::Projection &p) {
    basis[0]=(p.ax[1]*(1/p.tx)+p.ax[0]*.5)*(p.w-1);
    basis[1]=(p.ax[2]*(-1/p.ty)+p.ax[0]*.5)*(p.h-1);
    basis[2]=p.ax[0];
    double largest=0;for(auto &b:basis)for(double v:b)largest=std::max(largest,std::abs(v));
    std::frexp(largest,&exponent);
    for(unsigned j=0;j<3;j++)for(unsigned k=0;k<3;k++)fixed[j][k]=std::llround(std::ldexp(basis[j][k],44-exponent));
    std::array<boayo::V,5> planes{basis[2],basis[0],basis[1],basis[2]*(p.w-1)-basis[0],basis[2]*(p.h-1)-basis[1]};
    for(unsigned j=0;j<5;j++){
      double maximum=0;for(double v:planes[j])maximum=std::max(maximum,std::abs(v));
      if(maximum){
        for(unsigned k=0;k<3;k++)clip[j][k]=std::lround(planes[j][k]/maximum*8192);
        // Every triangle bound is <1024*max(|basis|), so the reference
        // scale is >= 2^min(22,19-exponent). Include 0.5-unit descriptor
        // rounding, amplified by source width/height for right/bottom planes.
        double factor=j==3?p.w:j==4?p.h:1;
        double rounding=32*factor*std::ldexp(1.,std::max(-22,exponent-19));
        clip_guard[j]=int64_t(std::ceil(96.*25000+rounding*0x1p26/maximum))+32768;
      }
    }
  }
  bool candidate(unsigned id)const{
    for(unsigned j=0;j<5;j++){
      int64_t v[3];
      for(unsigned k=0;k<3;k++){const auto *q=tile_vertex[id][k];v[k]=int64_t(clip[j][0])*q[0]+int64_t(clip[j][1])*q[1]+int64_t(clip[j][2])*q[2];}
      int64_t upper=std::max({94*v[0]+v[1]+v[2],v[0]+94*v[1]+v[2],v[0]+v[1]+94*v[2]})+std::max(int64_t(0),v[0]-2*v[1]+v[2]);
      // Covers Q13 plane/vertex error and hardware coefficient quantization.
      if(upper < -clip_guard[j])return false;
    }
    return true;
  }
};
// Q44 multiply using four RV32 M-extension products; no compiler 128-bit type.
// Bounds: |a| <= 96*2^44, |b| <= 2^44. All unsigned intermediates fit.
static uint64_t magnitude(int64_t v){return v<0?uint64_t(-v):uint64_t(v);}
static int64_t multiply_q44(int64_t a,int64_t b){
  uint64_t x=magnitude(a),y=magnitude(b);
  uint64_t lo=uint64_t(uint32_t(x))*uint32_t(y);
  uint64_t cross=(x>>32)*uint32_t(y)+uint64_t(uint32_t(x))*(y>>32);
  uint64_t high=(x>>32)*(y>>32);
  uint64_t value=(lo>>44)+(cross>>12)+(high<<20)+
    (((lo&((uint64_t(1)<<44)-1))+((cross&4095)<<32))>>44);
  return (a<0)!=(b<0)?-int64_t(value):int64_t(value);
}
static std::pair<boayo::Descriptor,bool> fast_descriptor(
    const boayo::Geometry &geometry,unsigned id,const boayo::Geometry::Projection &p,
    const FastProjection &transform,unsigned &fallbacks){
  using namespace boayo;
  // Conservative guard includes input quantization, truncated products and
  // floating reference reassociation. Ambiguous rounding uses the reference.
  constexpr uint64_t error=1024,bound_error=64*error;
  auto reference=[&](){++fallbacks;return geometry.descriptor(id,p);};
  int64_t c[3][4];uint64_t bound=0;
  for(unsigned j=0;j<3;j++){
    for(unsigned k=0;k<4;k++){
      const auto *a=tile_coefficients_q44[id][k],*b=transform.fixed[j];
      c[j][k]=multiply_q44(a[0],b[0])+multiply_q44(a[1],b[1])+multiply_q44(a[2],b[2]);
    }
    bound=std::max(bound,magnitude(c[j][0])+31*(magnitude(c[j][1])+magnitude(c[j][2]))+magnitude(c[j][3]));
  }
  if(bound<=bound_error)return reference();
  unsigned bits=64-__builtin_clzll(bound);
  uint64_t lower=uint64_t(1)<<(bits-1),upper=uint64_t(1)<<bits;
  if(bound-lower<=bound_error || upper-bound<=bound_error)return reference();
  int shift=std::max(int(bits)-29,44-transform.exponent-22);
  if(shift<1 || shift>61)return reference();
  uint64_t half=uint64_t(1)<<(shift-1),mask=(uint64_t(1)<<shift)-1;
  Descriptor d{};d[1]=id*256;d[2]=p.w;d[3]=p.h;d[4]=p.w;d[5]=1;
  for(unsigned j=0;j<3;j++)for(unsigned k=0;k<4;k++){
    uint64_t value=magnitude(c[j][k]),fraction=value&mask;
    if((fraction>half?fraction-half:half-fraction)<=error)return reference();
    int64_t rounded=int64_t((value+half)>>shift);
    d[6+j*4+k]=uint32_t(int32_t(c[j][k]<0?-rounded:rounded));
  }
  int64_t planes[5][4];
  for(int k=0;k<4;k++){
    auto u=int32_t(d[6+k]),v=int32_t(d[10+k]),z=int32_t(d[14+k]);
    planes[0][k]=z;planes[1][k]=u;planes[2][k]=v;
    planes[3][k]=int64_t(z)*(p.w-1)-u;planes[4][k]=int64_t(z)*(p.h-1)-v;
  }
  bool visible=true;
  for(int j=0;j<5;j++){
    const auto *v=planes[j];
    int64_t upper=std::max({v[0],v[0]+31*v[1],v[0]+31*(v[1]+v[2])})+std::max(int64_t(0),v[3]);
    visible &= j==0?upper>0:upper>=0;
  }
  return {d,visible};
}
