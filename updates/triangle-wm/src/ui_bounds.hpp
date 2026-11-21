#pragma once
struct UiBounds {int r0=0,r1=31,c0=0,c1=31;};
struct UiInverse {
 int64_t m[2][3][3]{};bool valid=false;
 explicit UiInverse(const boayo::Descriptor&d){
  uint64_t largest=0;
  for(int inv=0;inv<2;inv++){
   int64_t u=int32_t(d[6])+int64_t(inv)*int32_t(d[9]),v=int32_t(d[10])+int64_t(inv)*int32_t(d[13]),z=int32_t(d[14])+int64_t(inv)*int32_t(d[17]);
   int64_t ur=int32_t(d[7]),uc=int32_t(d[8]),vr=int32_t(d[11]),vc=int32_t(d[12]),zr=int32_t(d[15]),zc=int32_t(d[16]);
   for(auto q:{u,v,z,ur,uc,vr,vc,zr,zc})if(magnitude(q)>(1u<<30))return;
   auto &a=m[inv];
   a[0][0]=vc*z-v*zc;a[0][1]=u*zc-uc*z;a[0][2]=uc*v-u*vc;
   a[1][0]=v*zr-vr*z;a[1][1]=ur*z-u*zr;a[1][2]=u*vr-ur*v;
   a[2][0]=vr*zc-vc*zr;a[2][1]=uc*zr-ur*zc;a[2][2]=ur*vc-uc*vr;
   for(auto &row:a)for(auto q:row)largest=std::max(largest,magnitude(q));
  }
  if(!largest)return;
  unsigned shift=unsigned(std::max(0,64-__builtin_clzll(largest)-46));
  for(auto &a:m)for(auto &row:a)for(auto &q:row)q>>=shift;
  valid=true;
 }
 UiBounds bounds(int inv,int x0,int y0,int x1,int y1)const{
  if(!valid)return {};
  int64_t r0=INT64_MAX,r1=INT64_MIN,c0=INT64_MAX,c1=INT64_MIN;int sign=0;
  for(int x:{x0,x1})for(int y:{y0,y1}){
   int64_t q[3];for(int j=0;j<3;j++)q[j]=m[inv][j][0]*x+m[inv][j][1]*y+m[inv][j][2];
   // Quantization contributes <2600 units per dot product. Stay far from
   // projective poles; a three-cell guard covers truncation and rounding.
   if(magnitude(q[2])<(1u<<24))return {};
   int s=q[2]>0?1:-1;if(sign&&sign!=s)return {};sign=s;
   int64_t r=q[0]/q[2],c=q[1]/q[2];r0=std::min(r0,r);r1=std::max(r1,r);c0=std::min(c0,c);c1=std::max(c1,c);
  }
  return {int(std::clamp(r0-3,int64_t(0),int64_t(32))),int(std::clamp(r1+3,int64_t(-1),int64_t(31))),
          int(std::clamp(c0-3,int64_t(0),int64_t(32))),int(std::clamp(c1+3,int64_t(-1),int64_t(31)))};
 }
};
