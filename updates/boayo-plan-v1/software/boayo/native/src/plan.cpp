#include "plan.hpp"
#include "plan_revision.hpp"
#include <list>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
namespace boayo {
namespace {
thread_local uint64_t loads=0,hits=0,misses=0,rejected=0;
struct Entry {std::string key;std::shared_ptr<const ProjectionPlan> plan;};
thread_local std::list<Entry> cache;
uint32_t word(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint32_t crc(const uint8_t *p,size_t n,uint32_t c=~0u){
  static const auto table=[] {std::array<uint32_t,256> t{};for(unsigned i=0;i<256;++i){uint32_t v=i;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);t[i]=v;}return t;}();
  for(size_t i=0;i<n;++i)c=table[(c^p[i])&255]^(c>>8);return c;
}
std::shared_ptr<const ProjectionPlan> read_plan(const std::string &path,const std::string &key,unsigned width,unsigned height){
  int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return {};
  struct Close {int fd;~Close(){close(fd);}} closer{fd};
  std::array<uint8_t,4096> h{};struct stat st{};
  require(!fstat(fd,&st) && S_ISREG(st.st_mode) && pread(fd,h.data(),h.size(),0)==ssize_t(h.size()),"plan header");
  require(std::equal(h.begin(),h.begin()+8,"BOGPLN01"),"plan format");
  unsigned n=word(h.data()+8),bytes=word(h.data()+12),k=word(h.data()+16);
  require(n && n<=tiles && bytes>=n*84 && bytes<=tiles*(84+1024) && !(bytes&3) &&
          k==key.size() && k<=4096-28 && uint64_t(st.st_size)==4096ull+bytes,"plan bounds");
  require(std::equal(key.begin(),key.end(),h.begin()+28) &&
          ~crc(h.data()+28,k,crc(h.data(),24))==word(h.data()+24),"plan metadata CRC/key/revision");
  void *mapped=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,4096);require(mapped!=MAP_FAILED,"plan mmap");
  std::shared_ptr<const Texture> storage;
  try{storage=std::make_shared<Texture>(static_cast<const uint32_t *>(mapped),bytes/4);}
  catch(...){munmap(mapped,bytes);throw;}
  auto body=static_cast<const uint8_t *>(mapped);
  require(~crc(body,bytes)==word(h.data()+20),"plan payload CRC");
  auto plan=std::make_shared<ProjectionPlan>();plan->storage=std::move(storage);
  unsigned cursor=n*84;
  for(unsigned i=0;i<n;++i){auto record=body+i*84;
    unsigned id=word(record),kind=word(record+4),offset=word(record+8);
    require(id<tiles && (!i || id>plan->ids.back()) && kind<=2,"plan tile order/type");
    Descriptor d{};for(unsigned j=0;j<18;++j)d[j]=word(record+12+j*4);
    require(d[0]==0 && d[1]==id*256 && d[2]==width && d[3]==height && d[4]==width && d[5]==1,"plan descriptor bounds");
    const uint8_t *mask=nullptr;
    if(kind==2){require(offset==cursor && bytes-cursor>=1024,"plan mask offset");mask=body+offset;
      bool zero=false,one=false;for(unsigned j=0;j<1024;++j){require(mask[j]<=1,"plan mask value");zero|=mask[j]==0;one|=mask[j]==1;}
      require(zero && one,"plan mask classification");cursor+=1024;
    }else require(!offset,"plan uniform mask offset");
    plan->ids.push_back(id);plan->kinds.push_back(kind);plan->descriptors.push_back(d);plan->masks.push_back(mask);
  }
  require(cursor==bytes,"plan trailing data");return plan;
}
}
std::string projection_plan_key(const J &geometry,const J &shape){return J({{"geometry",geometry},{"shape",shape},{"revision",BOAYO_PLAN_REV}}).dump();}
std::shared_ptr<const ProjectionPlan> load_projection_plan(const J &geometry,const J &shape){
  const char *mode=getenv("BOAYO_PLANS");if(mode && std::string(mode)=="0")return {};
  auto key=projection_plan_key(geometry,shape);
  for(auto i=cache.begin();i!=cache.end();++i)if(i->key==key){++hits;cache.splice(cache.begin(),cache,i);return cache.front().plan;}
  uint32_t hash=2166136261u;for(unsigned char b:key){hash^=b;hash*=16777619u;}
  char name[32];std::snprintf(name,sizeof(name),"/p_%08x.bgp",hash);
  const char *dir=getenv("BOAYO_PLAN_DIR");
  std::vector<std::string> dirs=dir?std::vector<std::string>{dir}:std::vector<std::string>{"/usr/share/boayo/plans","/data/boayo-plans"};
  for(auto &d:dirs){try{auto plan=read_plan(d+name,key,geometry[0],geometry[1]);if(!plan)continue;
    ++loads;cache.push_front({key,plan});size_t bytes=0;for(auto i=cache.begin();i!=cache.end();){bytes+=i->plan->storage->size()*4;
      if(bytes>16u*1024*1024 && i!=cache.begin())i=cache.erase(i);else ++i;}return plan;
  }catch(const std::runtime_error &e){if(std::string(e.what()).find("plan ")!=0)throw;++rejected;std::fprintf(stderr,"BOAYO_PLAN_REJECT %s: %s\n",name,e.what());}}
  ++misses;return {};
}
J projection_plan_stats(){return {{"loads",loads},{"hits",hits},{"misses",misses},{"rejected",rejected},{"revision",BOAYO_PLAN_REV}};}
}
