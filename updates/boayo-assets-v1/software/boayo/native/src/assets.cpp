#include "assets.hpp"
#include "init_trace.hpp"
#include "rgb_fill.hpp"
#include <cstdlib>
#include <cstdio>
#include <list>
namespace boayo {
namespace {
struct Entry {std::string key;int w,h;Bytes pixels;std::shared_ptr<const Words> texture;};
thread_local std::list<Entry> cache;
thread_local uint64_t hits=0,loads=0,misses=0,rejected=0;
uint32_t word(const uint8_t *p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
uint32_t crc(const uint8_t *header,const Bytes &b) {
  static const auto table=[] {
    std::array<uint32_t,256> t{};
    for(unsigned i=0;i<256;++i) {uint32_t c=i;for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);t[i]=c;}
    return t;
  }();
  uint32_t c=~0u;
  for(unsigned i=0;i<24;++i)c=table[(c^header[i])&255]^(c>>8);
  for(auto v:b)c=table[(c^v)&255]^(c>>8);return ~c;
}
std::optional<Entry> read(const std::string &path,const std::string &key) {
  std::ifstream f(path,std::ios::binary);if(!f)return {};
  uint8_t header[28];f.read(reinterpret_cast<char *>(header),sizeof(header));
  require(f.gcount()==sizeof(header) && std::equal(header,header+8,"BUIRLE01"),"asset header");
  uint32_t w=word(header+8),h=word(header+12),k=word(header+16),runs=word(header+20);
  require(w && h && uint64_t(w)*h<=1024*1024 && k==key.size() && k<=4096 &&
          runs && runs<=uint64_t(w)*h,"asset dimensions");
  Bytes body(k+size_t(runs)*8);f.read(reinterpret_cast<char *>(body.data()),body.size());
  require(size_t(f.gcount())==body.size() && f.peek()==EOF && crc(header,body)==word(header+24),"asset length/CRC");
  require(std::equal(key.begin(),key.end(),body.begin()),"asset key collision/version mismatch");
  size_t count=size_t(w)*h,sum=0;
  for(unsigned i=0;i<runs;++i) {
    auto p=body.data()+k+i*8;auto n=word(p),color=word(p+4);
    require(n && n<=count-sum && !(color&0xff000000u),"asset run range");sum+=n;
  }
  require(sum==count,"asset incomplete pixels");
  Entry e{key,int(w),int(h),Bytes(count*3),{}};
  auto texture=std::make_shared<Words>(count);size_t pos=0;
  for(unsigned i=0;i<runs;++i) {
    auto p=body.data()+k+i*8;auto n=word(p),color=word(p+4);
    std::fill_n(texture->data()+pos,n,color);
    fill_rgb(e.pixels.data()+pos*3,size_t(n)*3,
             {uint8_t(color>>16),uint8_t(color>>8),uint8_t(color)});
    pos+=n;
  }
  e.texture=std::move(texture);return e;
}
} // namespace
std::optional<Surface> asset_surface(const J &request) {
  const char *mode=std::getenv("BOAYO_ASSETS");if(mode && std::string(mode)=="0")return {};
  std::string key=request.dump();
  for(auto i=cache.begin();i!=cache.end();++i)if(i->key==key) {
    ++hits;cache.splice(cache.begin(),cache,i);auto &e=cache.front();
    init_trace::mark("asset.cache",e.pixels.size());
    return Surface(e.w,e.h,e.pixels,e.texture);
  }
  uint32_t hash=2166136261u;for(unsigned char c:key){hash^=c;hash*=16777619u;}
  char name[32];std::snprintf(name,sizeof(name),"/%08x.bui",hash);
  const char *dir=std::getenv("BOAYO_ASSET_DIR");
  std::vector<std::string> dirs=dir?std::vector<std::string>{dir}:
      std::vector<std::string>{"/usr/share/boayo/assets","/data/boayo-assets"};
  for(const auto &d:dirs) {
    std::optional<Entry> entry;
    // Malformed/missing assets fall back; DMA errors during expansion propagate.
    try {entry=read(d+name,key);} catch(const std::runtime_error &error) {
      if(std::string(error.what()).find("asset ")!=0)throw;
      ++rejected;std::fprintf(stderr,"BOAYO_ASSET_REJECT %s: %s\n",name,error.what());continue;
    }
    if(!entry)continue;
    ++loads;cache.push_front(std::move(*entry));
    size_t memory=0;for(auto i=cache.begin();i!=cache.end();) {
      memory+=i->pixels.size()+i->texture->size()*4;
      if(memory>24u*1024*1024 && i!=cache.begin())i=cache.erase(i);else ++i;
    }
    auto &e=cache.front();init_trace::mark("asset.loaded",e.pixels.size());
    return Surface(e.w,e.h,e.pixels,e.texture);
  }
  ++misses;return {};
}
J asset_status(){return {{"loads",loads},{"cache_hits",hits},{"misses",misses},{"rejected",rejected}};}
} // namespace boayo
