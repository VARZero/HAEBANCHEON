#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace fixed {
// Signed Q39.24 storage. Geometry uses Q24 precision; angles are quantized Q2.
struct Number {
 int64_t raw=0;
 Number()=default;
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>
 Number(T v):raw(int64_t(v)*16777216){}
 static Number bits(int64_t v){Number n;n.raw=v;return n;}
 static Number ratio(int64_t a,int64_t b);
 explicit operator int()const{return int(raw/16777216);}
 explicit operator unsigned()const{return unsigned(raw/16777216);}
 explicit operator int64_t()const{return raw/16777216;}
 Number operator-()const{return bits(-raw);}
 Number&operator+=(Number b){raw+=b.raw;return *this;}
 Number&operator-=(Number b){raw-=b.raw;return *this;}
};
inline uint64_t mag(int64_t a){return a<0?uint64_t(-(a+1))+1:uint64_t(a);}
inline Number operator+(Number a,Number b){return Number::bits(a.raw+b.raw);}
inline Number operator-(Number a,Number b){return Number::bits(a.raw-b.raw);}
inline Number operator*(Number a,Number b){
 uint64_t x=mag(a.raw),y=mag(b.raw);
 uint64_t low=uint64_t(uint32_t(x))*uint32_t(y);
 uint64_t cross=(x>>32)*uint32_t(y)+uint64_t(uint32_t(x))*(y>>32);
 uint64_t high=(x>>32)*(y>>32);
 if(high>(uint64_t(INT64_MAX)>>40)||cross>(uint64_t(INT64_MAX)>>8))throw std::overflow_error("fixed multiply");
 uint64_t value=(low>>24)+(cross<<8);
 if(value>uint64_t(INT64_MAX)-(high<<40))throw std::overflow_error("fixed multiply");
 value+=high<<40;
 return Number::bits((a.raw<0)!=(b.raw<0)?-int64_t(value):int64_t(value));
}
inline Number Number::ratio(int64_t a,int64_t b){
 if(!b)throw std::domain_error("fixed divide by zero");
 uint64_t n=mag(a),d=mag(b),whole=n/d,rem=n%d;
 if(whole>uint64_t(INT64_MAX)>>24)throw std::overflow_error("fixed divide");
 uint64_t frac=0;
 if(rem<=(UINT64_MAX>>24))frac=(rem<<24)/d;
 else for(unsigned i=0;i<24;i++){frac<<=1;bool carry=rem>=(d-rem);if(carry){rem-=d-rem;frac|=1;}else rem+=rem;}
 uint64_t value=(whole<<24)|frac;
 return bits((a<0)!=(b<0)?-int64_t(value):int64_t(value));
}
inline Number operator/(Number a,Number b){return Number::ratio(a.raw,b.raw);}
inline bool operator==(Number a,Number b){return a.raw==b.raw;}
inline bool operator!=(Number a,Number b){return a.raw!=b.raw;}
inline bool operator<(Number a,Number b){return a.raw<b.raw;}
inline bool operator>(Number a,Number b){return b<a;}
inline bool operator<=(Number a,Number b){return !(b<a);}
inline bool operator>=(Number a,Number b){return !(a<b);}
inline Number abs(Number a){return a.raw<0?-a:a;}
template<class T>inline T abs(T a){return a<0?-a:a;}
inline bool isfinite(Number){return true;}
inline int64_t lround(Number a){return a.raw<0?-int64_t((mag(a.raw)+(1<<23))>>24):(a.raw+(1<<23))>>24;}
inline int64_t llround(Number a){return lround(a);}
// Exact equivalent of lround((value/largest)*8192). Normalized plane
// components need only 13 fractional bits; avoid general 64-bit division.
inline int32_t plane_q13(Number value,Number largest){
 uint64_t n=mag(value.raw),d=mag(largest.raw);
 if(largest.raw>0&&n<=d&&d<=0x7fffffffu){
  if(!n)return 0;if(n==d)return value.raw<0?-8192:8192;
  uint32_t rem=uint32_t(n),den=uint32_t(d),q=0;
  for(unsigned bit=0;bit<13;bit++){rem<<=1;q<<=1;if(rem>=den){rem-=den;q|=1;}}
  if(rem>=den-rem)++q;
  return value.raw<0?-int32_t(q):int32_t(q);
 }
 return int32_t(lround(value/largest*8192));
}
inline Number quarter(Number a){return Number::bits(lround(a*4)*(1<<22));}
inline Number rad(Number degrees){return degrees*Number::bits(52707179)/180;}
inline constexpr int64_t atan_table[31]={843314857,497837829,263043837,133525159,67021687,33543516,16775851,8388437,4194283,2097149,1048576,524288,262144,131072,65536,32768,16384,8192,4096,2048,1024,512,256,128,64,32,16,8,4,2,1};
inline std::array<Number,2> sincos(Number angle){
 constexpr int64_t pi=3373259426LL,half=1686629713LL,tau=6746518852LL;
 int64_t z=angle.raw*64;z%=tau;if(z>pi)z-=tau;if(z<-pi)z+=tau;
 int sign=1;if(z>half){z-=pi;sign=-1;}if(z<-half){z+=pi;sign=-1;}
 int64_t x=652032874,y=0;
 for(unsigned i=0;i<31;i++){int64_t old=x;if(z>=0){x-=y>>i;y+=old>>i;z-=atan_table[i];}else{x+=y>>i;y-=old>>i;z+=atan_table[i];}}
 return {Number::bits(sign*(y/64)),Number::bits(sign*(x/64))};
}
inline Number sin(Number a){return sincos(a)[0];}
inline Number cos(Number a){return sincos(a)[1];}
inline Number tan(Number a){auto q=sincos(a);return q[0]/q[1];}
#ifdef FIXED_TRIG_TABLES
#include "fixed_trig_table.hpp"
#endif
inline std::array<Number,2> sincos_degrees(Number a){
#ifdef FIXED_TRIG_TABLES
 if(a.raw>=-180LL*16777216&&a.raw<=180LL*16777216&&(a.raw&4194303)==0){
  auto q=degree_sincos_raw[a.raw/4194304+720];return {Number::bits(q[0]),Number::bits(q[1])};
 }
#endif
 return sincos(rad(a));
}
inline Number tan_degrees(Number a){
#ifdef FIXED_TRIG_TABLES
 if(a.raw>=0&&a.raw<=64LL*16777216&&(a.raw&2097151)==0)return Number::bits(degree_tan_raw[a.raw/2097152]);
#endif
 return tan(rad(a));
}
inline Number atan2(Number yy,Number xx){
 int64_t x=xx.raw,y=yy.raw,z=0;
 if(!x&&!y)return {};
 while(mag(x)>uint64_t(1)<<45||mag(y)>uint64_t(1)<<45){x>>=1;y>>=1;}
 if(x<0){z=y>=0?3373259426LL:-3373259426LL;x=-x;y=-y;}
 for(unsigned i=0;i<31;i++){int64_t old=x;if(y>0){x+=y>>i;y-=old>>i;z+=atan_table[i];}else if(y<0){x-=y>>i;y+=old>>i;z-=atan_table[i];}}
 return Number::bits(z/64);
}
inline Number atan(Number a){return atan2(a,1);}
inline uint64_t root(uint64_t n){uint64_t r=0,bit=uint64_t(1)<<62;while(bit>n)bit>>=2;while(bit){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}
inline Number sqrt(Number a){if(a.raw<0)throw std::domain_error("fixed sqrt");uint64_t n=a.raw;unsigned shift=0;while(n>(uint64_t(1)<<39)){n>>=2;++shift;}return Number::bits(int64_t(root(n<<24)<<shift));}
inline Number hypot(Number a,Number b){return sqrt(a*a+b*b);}
inline Number asin(Number a){return atan2(a,sqrt(std::max(Number(0),Number(1)-a*a)));}
inline Number azimuth(Number a){while(a>=180)a-=360;while(a<-180)a+=360;return a;}
using Vec=std::array<Number,3>;
inline Vec operator+(Vec a,const Vec&b){for(int i=0;i<3;i++)a[i]+=b[i];return a;}
inline Vec operator-(Vec a,const Vec&b){for(int i=0;i<3;i++)a[i]-=b[i];return a;}
inline Vec operator*(Vec a,Number b){for(auto&v:a)v=v*b;return a;}
inline Number dot(const Vec&a,const Vec&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec cross(const Vec&a,const Vec&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline Vec unit(Vec a){return a*(Number(1)/sqrt(dot(a,a)));}
inline std::array<Vec,3> axes(Number yaw,Number pitch,Number roll=0){
 auto a=sincos_degrees(yaw),p=sincos_degrees(pitch),r=sincos_degrees(roll);
 Vec c{p[1]*a[0],p[0],-p[1]*a[1]},right{a[1],0,a[0]},up=cross(right,c);
 return {c,right*r[1]+up*r[0],up*r[1]-right*r[0]};
}
// IEEE conversion is confined to the existing JSON number boundary. No
// floating arithmetic is used for extraction; formatting stays in nlohmann.
inline void from_json(const boayo::J&j,Number&n){
 if(j.is_number_integer()){int64_t v=j.get<int64_t>();if(v>100000000||v<-100000000)throw std::out_of_range("fixed JSON");n=Number(v);return;}
 if(!j.is_number())throw std::invalid_argument("numeric fixed value required");
 double value=j.get<double>();uint64_t bits;std::memcpy(&bits,&value,8);
 unsigned exponent=(bits>>52)&2047;if(exponent==2047)throw std::out_of_range("nonfinite fixed JSON");
 uint64_t m=bits&((uint64_t(1)<<52)-1);if(exponent)m|=uint64_t(1)<<52;
 int shift=int(exponent?exponent:1)-1023-52+24;
 if(shift>8)throw std::out_of_range("fixed JSON overflow");
 uint64_t v=shift>=0?m<<shift:shift<=-64?0:m>>-shift;
 n=Number::bits(bits>>63?-int64_t(v):int64_t(v));
}
inline void to_json(boayo::J&j,const Number&n){
 if(!(n.raw&0xffffff)){j=n.raw/16777216;return;}
 uint64_t v=mag(n.raw);unsigned bit=63-__builtin_clzll(v);uint64_t mant=bit<=52?v<<(52-bit):v>>(bit-52);
 uint64_t bits=(uint64_t(n.raw<0)<<63)|(uint64_t(int(bit)-24+1023)<<52)|(mant&((uint64_t(1)<<52)-1));
 double value;std::memcpy(&value,&bits,8);j=value;
}
} // namespace fixed
