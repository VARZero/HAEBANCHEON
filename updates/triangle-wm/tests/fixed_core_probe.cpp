#include "geometry.hpp"
#include "fixed_geometry.hpp"
// Export a live numerical path so the compiler cannot discard the math audit.
extern "C" uint32_t fixed_core_probe(int16_t yaw_q2,int16_t pitch_q2,unsigned tile){
 Fx y=Fx::ratio(yaw_q2,4),p=Fx::ratio(pitch_q2,4);
 auto camera=fx_camera(y,p);
 FxGeometry::Projection projection(240,144,y,p,0,20,14);FxProjection transform(projection);
 auto descriptor=fx_descriptor(tile%4220,transform);
 return uint32_t(camera[0])^descriptor.first[6]^uint32_t(descriptor.second);
}
