#include "assets.hpp"
#include "init_trace.hpp"
#include <cstdlib>
#include <cstdio>
#include <list>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace boayo {
namespace {
struct Entry {std::string key;int w,h;std::shared_ptr<const Texture> texture;};
thread_local std::list<Entry> cache;
thread_local uint64_t hits=0,loads=0,misses=0,rejected=0;
uint32_t word(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint32_t crc(const uint8_t *p,size_t n,uint32_t c=~0u){
  static const auto table=[] {std::array<uint32_t,256> t{};for(unsigned i=0;i<256;++i){uint32_t v=i;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);t[i]=v;}return t;}();
  for(size_t i=0;i<n;++i)c=table[(c^p[i])&255]^(c>>8);return c;
}
std::optional<Entry> read(const std::string &path,const std::string &key){
  int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return {};
  struct Close {int fd;~Close(){close(fd);}} closer{fd};
  std::array<uint8_t,4096> h{};struct stat st{};
  require(!fstat(fd,&st) && S_ISREG(st.st_mode) && pread(fd,h.data(),h.size(),0)==ssize_t(h.size()),"asset header");
  require(std::equal(h.begin(),h.begin()+8,"BUITEX01"),"asset format");
  uint32_t w=word(h.data()+8),height=word(h.data()+12),k=word(h.data()+16),bytes=word(h.data()+20);
  require(w && height && uint64_t(w)*height<=1024*1024 && uint64_t(w)*height*4==bytes &&
          k==key.size() && k<=4096-32 && uint64_t(st.st_size)==4096ull+bytes,"asset dimensions/length");
  require(std::equal(key.begin(),key.end(),h.begin()+32) &&
          ~crc(h.data()+32,k,crc(h.data(),28))==word(h.data()+28),"asset metadata CRC/key");
  auto ptr=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,4096);
  require(ptr!=MAP_FAILED,"asset mmap");
  std::shared_ptr<const Texture> texture;
  try{texture=std::make_shared<Texture>(static_cast<const uint32_t *>(ptr),bytes/4);}
  catch(...){munmap(ptr,bytes);throw;}
  // Optional diagnostics only: normal rendering never scans/copies payload on CPU.
  const char *verify=getenv("BOAYO_ASSET_VERIFY");
  if(verify && std::string(verify)=="1")require(~crc(static_cast<const uint8_t *>(ptr),bytes)==word(h.data()+24),"asset payload CRC");
  return Entry{key,int(w),int(height),std::move(texture)};
}
}
std::optional<Surface> asset_surface(const J &request){
  const char *mode=getenv("BOAYO_ASSETS");if(mode && std::string(mode)=="0")return {};
  std::string key=request.dump();
  for(auto i=cache.begin();i!=cache.end();++i)if(i->key==key){
    ++hits;cache.splice(cache.begin(),cache,i);auto &e=cache.front();init_trace::mark("asset.cache",e.texture->size()*4);
    return Surface(e.w,e.h,{},e.texture);
  }
  uint32_t hash=2166136261u;for(unsigned char c:key){hash^=c;hash*=16777619u;}
  char name[32];std::snprintf(name,sizeof(name),"/%08x.bui",hash);
  const char *dir=getenv("BOAYO_ASSET_DIR");
  std::vector<std::string> dirs=dir?std::vector<std::string>{dir}:std::vector<std::string>{"/usr/share/boayo/assets","/data/boayo-assets"};
  init_trace::mark("asset.map.begin");
  for(const auto &d:dirs){
    std::optional<Entry> entry;
    try{entry=read(d+name,key);}catch(const std::runtime_error &error){
      if(std::string(error.what()).find("asset ")!=0)throw;
      ++rejected;std::fprintf(stderr,"BOAYO_ASSET_REJECT %s: %s\n",name,error.what());continue;}
    if(!entry)continue;
    ++loads;cache.push_front(std::move(*entry));size_t memory=0;
    for(auto i=cache.begin();i!=cache.end();){memory+=i->texture->size()*4;
      if(memory>24u*1024*1024 && i!=cache.begin())i=cache.erase(i);else ++i;}
    auto &e=cache.front();init_trace::mark("asset.loaded",e.texture->size()*4);
    return Surface(e.w,e.h,{},e.texture);
  }
  ++misses;return {};
}
J asset_status(){return {{"loads",loads},{"cache_hits",hits},{"misses",misses},{"rejected",rejected},{"rgb_materializations",rgb_materializations},{"format","raw-mapped-00RRGGBB"}};}
}
