#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
namespace key_transport {
inline const char*name(unsigned code){static const char*names[]={"","Home","ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Enter"};return code<7?names[code]:"";}
inline uint32_t load(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline void put(uint8_t*p,uint32_t n){for(int i=0;i<4;i++)p[i]=uint8_t(n>>(i*8));}
inline uint32_t crc(const uint8_t*p,size_t size){uint32_t c=~0u;while(size--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
using Frame=std::array<uint8_t,20>;
struct Event {uint32_t session=0,sequence=0;unsigned code=0,value=0;};
inline bool decode(const Frame&f,Event&e){
 if(memcmp(f.data(),"KBD1",4)||load(f.data()+16)!=crc(f.data(),16)||f[14]||f[15])return false;
 e={load(f.data()+4),load(f.data()+8),f[12],f[13]};return e.session&&e.code<=6&&e.value<=2&&(!e.code||e.sequence);
}
inline Frame acknowledge(uint32_t session,uint32_t sequence){Frame f{};memcpy(f.data(),"KACK",4);put(f.data()+4,session);put(f.data()+8,sequence);put(f.data()+16,crc(f.data(),16));return f;}
struct SerialParser {
 std::string escape,line;bool cr=false;uint64_t last=0;
 std::vector<std::string> feed(const uint8_t*p,size_t n,uint64_t now){
  std::vector<std::string> keys;if(!escape.empty()&&now-last>200000)escape.clear();last=now;
  auto named=[&](){std::string s=line;if(s.rfind("KEY ",0)==0)s.erase(0,4);for(unsigned k=1;k<=6;k++)if(s==name(k))keys.push_back(s);line.clear();};
  while(n--){unsigned c=*p++;
   if(c==27){escape="\x1b";line.clear();cr=false;continue;}
   if(!escape.empty()){
    escape+=char(c);if(escape.size()==2&&(c=='['||c=='O'))continue;
    if(c>='0'&&c<='9'&&escape.size()<8)continue;
    std::string key;if(escape=="\x1b[A"||escape=="\x1bOA")key="ArrowUp";else if(escape=="\x1b[B"||escape=="\x1bOB")key="ArrowDown";else if(escape=="\x1b[C"||escape=="\x1bOC")key="ArrowRight";else if(escape=="\x1b[D"||escape=="\x1bOD")key="ArrowLeft";else if(escape=="\x1b[H"||escape=="\x1bOH"||escape=="\x1b[1~"||escape=="\x1b[7~")key="Home";
    if(!key.empty())keys.push_back(key);escape.clear();continue;
   }
   if(c=='\r'||c=='\n'){if(!line.empty())named();else if(c=='\r'||!cr)keys.push_back("Enter");cr=c=='\r';}
   else {cr=false;if(c>=32&&c<=126&&line.size()<64)line+=char(c);else line.clear();}
  }return keys;
 }
};
}
