#pragma once
// Exact projected sample coordinates, cached lazily per tile/output view.
// Fraction flags preserve the original inclusive rational bounds before rounding.
struct UiSamples {
 std::array<uint32_t,1024> cells{};
 // n/d is a visible screen coordinate (0..1279). Use RV32's native
 // divider for an estimate, then correct against the original integers.
 // A denominator of at least 4096 bounds the estimate error to one pixel.
 static unsigned quotient(uint64_t n,uint64_t d){
  if(n<d)return 0;
  unsigned shift=unsigned(std::max(0,64-__builtin_clzll(n)-32));
  if(!shift)return uint32_t(n)/uint32_t(d);
  uint64_t denominator=d>>shift;
  if(denominator<4096)return unsigned(n/d);
  unsigned q=uint32_t(n>>shift)/uint32_t(denominator);
  if(uint64_t(q)*d>n)--q;
  else if(uint64_t(q+1)*d<=n)++q;
  return q;
 }
 uint32_t get(const boayo::Descriptor&d,unsigned cell,int row,int col,int inv){
  auto &packed=cells[cell];if(packed)return packed;
  int64_t q[3];for(int k=0;k<3;k++)q[k]=int32_t(d[6+4*k])+int64_t(row)*int32_t(d[7+4*k])+int64_t(col)*int32_t(d[8+4*k])+int64_t(inv)*int32_t(d[9+4*k]);
  auto u=q[0],v=q[1],z=q[2];
  if(z<=0||u<0||v<0||u>z*1279||v>z*719)return packed=UINT32_MAX;
  int x=int(quotient(u,z)),y=int(quotient(v,z));auto rx=u-int64_t(x)*z,ry=v-int64_t(y)*z;
  return packed=(1u<<31)|unsigned(x)|(unsigned(y)<<11)|(unsigned(rx!=0)<<21)|(unsigned(ry!=0)<<22)|
    (unsigned(rx>=(z+1)/2)<<23)|(unsigned(ry>=(z+1)/2)<<24);
 }
 static bool near(uint32_t p,int x,int y,int radius){
  int fx=p&2047,fy=(p>>11)&1023;
  return fx>=x-radius&&fx+int((p>>21)&1)<=x+radius&&fy>=y-radius&&fy+int((p>>22)&1)<=y+radius;
 }
 static int x(uint32_t p){return int(p&2047)+int((p>>23)&1);}
 static int y(uint32_t p){return int((p>>11)&1023)+int((p>>24)&1);}
};
