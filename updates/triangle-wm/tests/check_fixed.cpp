// Floating arithmetic is the independent HOST reference only.
#include "geometry.hpp"
#include "fixed_geometry.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
static double value(Fx q){return double(q.raw)/16777216;}
int main(){
 std::mt19937_64 rng(1717);double trig_error=0,coordinate_error=0;unsigned checked=0;
 for(int i=0;i<10000;i++){
  int64_t a=int64_t(rng()%20000000001ULL)-10000000000LL,b=int64_t(rng()%20000000001ULL)-10000000000LL;if(!b)b=1;
  auto m=Fx::bits(a)*Fx::bits(b),d=Fx::bits(a)/Fx::bits(b);
  __int128 expected=__int128(a)*b/16777216;
  assert(m.raw==expected);assert(d.raw==__int128(a)*16777216/b);
 }
 for(int q=-720;q<=720;q++){
  Fx degrees=Fx::ratio(q,4),r=fixed::rad(degrees);auto sc=fixed::sincos(r);
  trig_error=std::max({trig_error,std::abs(value(sc[0])-std::sin(q*.25*boayo::pi/180)),std::abs(value(sc[1])-std::cos(q*.25*boayo::pi/180))});
  assert(fixed::quarter(degrees)==degrees);
 }
 assert(trig_error<0.000002);
 boayo::Geometry reference;
 for(int y:{-160,-20,0,25,170})for(int pitch:{-70,0,60})for(int width:{8,20,90}){
  FxGeometry::Projection p(240,144,y,pitch,0,width,14);FxProjection t(p);
  boayo::Geometry::Projection ref(240,144,y,pitch,0,width,14);
  for(unsigned tile=0;tile<4220;tile+=17){
   auto a=fx_descriptor(tile,t).first;auto original=reference.descriptor(tile,ref);auto b=original.first;
   assert(!original.second||t.candidate(tile));
   for(unsigned cell=0;cell<1024;cell+=47){auto u=boayo::coordinates(a,cell),v=boayo::coordinates(b,cell);
    if(!boayo::covered(a,u)||!boayo::covered(b,v))continue;
    coordinate_error=std::max({coordinate_error,std::abs(double(u[0])/u[2]-double(v[0])/v[2]),std::abs(double(u[1])/u[2]-double(v[1])/v[2])});++checked;
   }
  }
 }
 std::cout<<"FIXED_REFERENCE trig_error="<<trig_error<<" projected_pixel_error="<<coordinate_error<<" samples="<<checked<<"\n";
 assert(checked>1000&&coordinate_error<0.1);
 // JSON boundary, signed angles, exact Q2 half-step policy and input rejection.
 for(int n=-720;n<=720;n++){Fx a=Fx::ratio(n,4);boayo::J j=a;assert(j.get<Fx>()==a);}
 bool rejected=false;try{boayo::J("bad").get<Fx>();}catch(...){rejected=true;}assert(rejected);
 puts("PASS_FIXED: integer multiply/divide, CORDIC, Q2 angles, projection reference, JSON roundtrip");
}
