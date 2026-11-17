#pragma once
#include "geometry.hpp"
namespace boayo {
#include "culling_bounds.inc"
// A rejection-only filter. Kept tiles still use the unchanged descriptor math.
inline std::array<bool,132> candidate_groups(const Geometry::Projection&p) {
  std::array<bool,132> out{};out.fill(true);
  if(getenv("BOAYO_GEOMETRY_REFERENCE"))return out;
  if(p.tx<.001 || p.ty<.001)return out;
  const double w=p.w-1,h=p.h-1;
  std::array<V,5> n{p.ax[0], (p.ax[1]*(1/p.tx)+p.ax[0]*.5)*w,
    (p.ax[2]*(-1/p.ty)+p.ax[0]*.5)*h, {}, {}};
  n[3]=p.ax[0]*w-n[1];n[4]=p.ax[0]*h-n[2];
  std::array<double,5> l{};double largest=1;
  for(unsigned j=0;j<5;++j){for(unsigned k=0;k<3;++k)l[j]+=std::abs(n[j][k]);largest=std::max(largest,l[j]);}
  if(!std::isfinite(largest) || largest>1e6)return out;
  // Original coefficient bound <= 472*L. 512 includes arithmetic slack.
  // scale >= min(2^22, 2^28/bound). Rounding each plane coefficient and
  // its affine upper bound contributes <=32*max(width,height) units.
  // Use twice that allowance plus a separate floating-point slack.
  double inv_scale=std::max(0x1p-22,512*largest*0x1p-28);
  double slack=64*std::max(p.w,p.h)*inv_scale+largest*std::max(p.w,p.h)*1e-8;
  for(unsigned g=0;g<out.size();++g){const auto &b=tile_group_bounds[g];
    for(unsigned j=0;j<5;++j){double maximum=0;
      for(unsigned k=0;k<3;++k)maximum+=n[j][k]*b[k+(n[j][k]>=0?3:0)];
      // Each original affine upper endpoint is 96 times a convex vertex
      // combination, plus max(0,q0-2*q1+q2), bounded by 4*L.
      if(96*maximum+4.00001*l[j]<-slack){out[g]=false;break;}
    }
  }
  return out;
}
}
