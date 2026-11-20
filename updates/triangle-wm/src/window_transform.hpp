#pragma once
#include "geometry.hpp"

struct WindowTransform {
 bool ready=false;double yaw=0,pitch=0,width=0,height=0,padding=0;
 std::array<boayo::V,3> ax{};double tx=0,ty=0,padded_x=0,padded_y=0;
 void update(double y,double p,double w,double h,double pad){
  using namespace boayo;
  if(!ready || yaw!=y || pitch!=p)ax=axes(y,p);
  if(!ready || width!=w)tx=2*std::tan(rad(w/2));
  if(!ready || height!=h)ty=2*std::tan(rad(h/2));
  if(!ready || width!=w || padding!=pad)padded_x=std::tan(rad((w+2*pad)/2));
  if(!ready || height!=h || padding!=pad)padded_y=std::tan(rad((h+2*pad)/2));
  yaw=y;pitch=p;width=w;height=h;padding=pad;ready=true;
 }
 boayo::Geometry::Projection projection(unsigned w,unsigned h)const{
  // Construct the placeholder only once, then replace all projection fields.
  static const boayo::Geometry::Projection seed(1,1,0,0,0,20,14);
  auto p=seed;p.w=w;p.h=h;p.ax=ax;p.tx=tx;p.ty=ty;return p;
 }
};
