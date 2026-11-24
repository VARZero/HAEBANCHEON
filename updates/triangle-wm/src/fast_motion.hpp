#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
namespace boayo::fast_motion {
constexpr uint32_t magic=0x31525450u; // PTR1 within the existing BWM1 frame.
struct Motion {uint32_t sequence;unsigned x,y;};
inline uint32_t word(const char*p){return uint32_t(uint8_t(p[0]))|uint32_t(uint8_t(p[1]))<<8|uint32_t(uint8_t(p[2]))<<16|uint32_t(uint8_t(p[3]))<<24;}
inline bool recognizable(const std::string&s){return s.size()>=4&&word(s.data())==magic;}
inline bool decode(const std::string&s,Motion&m){
 if(s.size()!=12||!recognizable(s))return false;
 m={word(s.data()+4),unsigned(uint8_t(s[8]))|(unsigned(uint8_t(s[9]))<<8),unsigned(uint8_t(s[10]))|(unsigned(uint8_t(s[11]))<<8)};
 return m.sequence&&m.x<1280&&m.y<720;
}
inline std::array<char,20> frame(Motion m){
 std::array<char,20> out{};auto put=[&](unsigned at,uint32_t n){for(unsigned k=0;k<4;k++)out[at+k]=char(n>>(k*8));};
 put(0,0x314d5742u);put(4,12);put(8,magic);put(12,m.sequence);
 out[16]=char(m.x);out[17]=char(m.x>>8);out[18]=char(m.y);out[19]=char(m.y>>8);return out;
}
inline bool serial_line(const std::string&s,Motion&m){
 if(s.empty()||s[0]!='@')return false;const char*p=s.data()+1,*end=s.data()+s.size();uint32_t v[3];
 for(unsigned k=0;k<3;k++){
  auto r=std::from_chars(p,end,v[k]);if(r.ec!=std::errc())return false;p=r.ptr;
  if(k<2){if(p==end||*p!=' ')return false;++p;}
 }
 if(p!=end||!v[0]||v[1]>=1280||v[2]>=720)return false;
 m={v[0],v[1],v[2]};return true;
}
}
