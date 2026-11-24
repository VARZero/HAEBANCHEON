#pragma once
#include "common.hpp"
#include <cstring>
namespace boayo::wire {
constexpr uint32_t magic=0x314d5742,maximum=65536;
inline bool json_mode(){static bool v=[](){auto p=getenv("TRIANGLE_WIRE");return p&&std::string(p)=="json";}();return v;}
inline bool optimized(){static bool v=[](){auto p=getenv("TRIANGLE_WIRE_OPT");return !(p&&std::string(p)=="0");}();return v;}
inline uint32_t word(const char*p){return uint8_t(p[0])|(uint32_t(uint8_t(p[1]))<<8)|(uint32_t(uint8_t(p[2]))<<16)|(uint32_t(uint8_t(p[3]))<<24);}
inline void append(std::string&s,uint32_t v){for(unsigned i=0;i<4;i++)s.push_back(char(v>>(8*i)));}
inline const std::string&fixed_key(){static const std::string key("\0q24",4);return key;}
inline const std::string&angle_key(){static const std::string key("\0a2",3);return key;}
inline bool angular(const std::string&key){return key=="yaw"||key=="pitch"||key=="roll"||key=="azimuth"||key=="elevation"||key=="width_deg"||key=="height_deg"||key=="padding_deg";}
inline int64_t fixed_number(const J&j){
 double value=j.get<double>();uint64_t b;std::memcpy(&b,&value,8);
 unsigned e=(b>>52)&2047;require(e!=2047,"nonfinite wire number");
 uint64_t m=b&((uint64_t(1)<<52)-1);if(e)m|=uint64_t(1)<<52;
 int shift=int(e?e:1)-1023-52+24;require(shift<0||shift<63&&m<=(uint64_t(INT64_MAX)>>shift),"wire fixed overflow");
 uint64_t n=shift>=0?m<<shift:shift<=-64?0:m>>-shift;
 return b>>63?-int64_t(n):int64_t(n);
}
inline J number(int64_t raw){
 if(!(raw&0xffffff))return raw/16777216;
 uint64_t n=raw<0?uint64_t(-(raw+1))+1:uint64_t(raw);unsigned k=63-__builtin_clzll(n);
 uint64_t m=k<=52?n<<(52-k):n>>(k-52);
 uint64_t b=(uint64_t(raw<0)<<63)|(uint64_t(int(k)-24+1023)<<52)|(m&((uint64_t(1)<<52)-1));
 double value;std::memcpy(&value,&b,8);return value;
}
inline J pack(const J&j,unsigned depth=0){
 require(depth<=32,"wire nesting limit");
 if(j.is_number_float())return J{{fixed_key(),fixed_number(j)}};
 if(j.is_object()){
  require(!j.contains(fixed_key())&&!j.contains(angle_key()),"reserved wire key");J r=J::object();
  for(auto it=j.begin();it!=j.end();++it){
   if(angular(it.key())&&it.value().is_number()){
    int64_t raw;
    if(it.value().is_number_float())raw=fixed_number(it.value());
    else{int64_t degrees=it.value().get<int64_t>();require(degrees>=-8192&&degrees<8192,"wire angle overflow");raw=degrees*16777216;}
    int64_t q2=raw<0?-((-raw+(1<<21))>>22):(raw+(1<<21))>>22;
    require(q2>=-32768&&q2<=32767,"wire angle overflow");r[it.key()]=J{{angle_key(),q2}};
   }else r[it.key()]=pack(it.value(),depth+1);
  }return r;
 }
 if(j.is_array()){J r=J::array();for(auto&v:j)r.push_back(pack(v,depth+1));return r;}
 return j;
}
inline J unpack(const J&j,unsigned depth=0){
 require(depth<=32&&!j.is_number_float(),"invalid wire number/nesting");
 if(j.is_object()){
  if(j.contains(angle_key())){require(j.size()==1&&j.at(angle_key()).is_number_integer(),"invalid wire angle tag");int64_t q2=j.at(angle_key()).get<int64_t>();require(q2>=-32768&&q2<=32767,"wire angle overflow");return number(q2*(1<<22));}
  if(j.contains(fixed_key())){require(j.size()==1&&j.at(fixed_key()).is_number_integer(),"invalid fixed wire tag");return number(j.at(fixed_key()).get<int64_t>());}
  J r=J::object();for(auto it=j.begin();it!=j.end();++it)r[it.key()]=unpack(it.value(),depth+1);return r;
 }
 if(j.is_array()){J r=J::array();for(auto&v:j)r.push_back(unpack(v,depth+1));return r;}
 return j;
}
inline void pack_inplace(J&j,unsigned depth=0){
 require(depth<=32,"wire nesting limit");
 if(j.is_number_float()){j=J{{fixed_key(),fixed_number(j)}};return;}
 if(j.is_object()){
  require(!j.contains(fixed_key())&&!j.contains(angle_key()),"reserved wire key");
  for(auto it=j.begin();it!=j.end();++it){
   if(angular(it.key())&&it.value().is_number()){
    int64_t raw;
    if(it.value().is_number_float())raw=fixed_number(it.value());
    else{int64_t degrees=it.value().get<int64_t>();require(degrees>=-8192&&degrees<8192,"wire angle overflow");raw=degrees*16777216;}
    int64_t q2=raw<0?-((-raw+(1<<21))>>22):(raw+(1<<21))>>22;
    require(q2>=-32768&&q2<=32767,"wire angle overflow");it.value()=J{{angle_key(),q2}};
   }else pack_inplace(it.value(),depth+1);
  }
 }else if(j.is_array())for(auto&v:j)pack_inplace(v,depth+1);
}
inline void unpack_inplace(J&j,unsigned depth=0){
 require(depth<=32&&!j.is_number_float(),"invalid wire number/nesting");
 if(j.is_object()){
  if(j.contains(angle_key())){require(j.size()==1&&j.at(angle_key()).is_number_integer(),"invalid wire angle tag");int64_t q=j.at(angle_key()).get<int64_t>();require(q>=-32768&&q<=32767,"wire angle overflow");j=number(q*(1<<22));return;}
  if(j.contains(fixed_key())){require(j.size()==1&&j.at(fixed_key()).is_number_integer(),"invalid fixed wire tag");j=number(j.at(fixed_key()).get<int64_t>());return;}
  for(auto&v:j)unpack_inplace(v,depth+1);
 }else if(j.is_array())for(auto&v:j)unpack_inplace(v,depth+1);
}
inline bool invalid(const std::string&s){if(json_mode())return s.size()>maximum&&s.find('\n')==std::string::npos;return s.size()>=8&&(word(s.data())!=magic||!word(s.data()+4)||word(s.data()+4)>maximum);}
inline bool ready(const std::string&s){if(json_mode())return s.find('\n')!=std::string::npos;return s.size()>=8&&(invalid(s)||s.size()>=8+word(s.data()+4));}
inline unsigned frame_size(const std::string&s){require(ready(s)&&!invalid(s),"invalid wire frame");return json_mode()?unsigned(s.find('\n')+1):8+word(s.data()+4);}
inline std::string peek(const std::string&s){unsigned n=frame_size(s);return json_mode()?s.substr(0,n-1):s.substr(8,n-8);}
inline std::string take(std::string&s){unsigned n=frame_size(s),offset=json_mode()?0:8;auto p=s.substr(offset,n-offset-(json_mode()?1:0));s.erase(0,n);return p;}
inline J decode(const std::string&p){require(p.size()<=maximum,"wire payload limit");if(json_mode())return J::parse(p);auto j=J::from_cbor(p.begin(),p.end(),true,true);if(!optimized())return unpack(j);unpack_inplace(j);return j;}
inline std::string frame(const std::vector<uint8_t>&data){require(!data.empty()&&data.size()<=maximum,"wire payload limit");std::string s;append(s,magic);append(s,data.size());s.append(reinterpret_cast<const char*>(data.data()),data.size());return s;}
inline std::string encode(const J&j){
 if(json_mode())return j.dump()+"\n";
 if(!optimized())return frame(J::to_cbor(pack(j)));
 J temporary=j;pack_inplace(temporary);return frame(J::to_cbor(temporary));
}
inline std::string encode(J&&j){
 if(json_mode())return j.dump()+"\n";
 if(!optimized())return frame(J::to_cbor(pack(j)));
 pack_inplace(j);return frame(J::to_cbor(j));
}
} // namespace boayo::wire
