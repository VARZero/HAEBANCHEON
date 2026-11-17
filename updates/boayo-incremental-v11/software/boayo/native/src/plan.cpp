#include "plan.hpp"
#include "plan_revision.hpp"
#include "../../../bosio/include/bosio_uapi.h"
#include <list>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
namespace boayo {
Words make_device_plan(const ProjectionPlan &plan){
  static_assert(sizeof(bo_batch_entry)==160,"device plan batch ABI");
  Words data(18);data[0]=0x31594452;data[1]=1;data[3]=plan.payload_crc;
  std::vector<unsigned> direct,masked;
  for(unsigned i=0;i<plan.ids.size();++i){
    if(plan.kinds[i]==1)direct.push_back(i);
    else if(plan.kinds[i]==2)masked.push_back(i);
  }
  require(direct.size()+masked.size()<=128,"plan ready job limit");
  for(unsigned phase=0;phase<2;++phase){auto &ids=phase?masked:direct;
    for(unsigned core=0;core<2;++core){unsigned slot=phase*2+core;data[8+slot]=data.size();
      for(unsigned j=core;j<ids.size();j+=2){unsigned i=ids[j];auto d=plan.descriptors[i];
        d[1]=header+(phase?j:plan.ids[i])*256;d[5]=1;
        data.insert(data.end(),d.begin(),d.end());++data[4+slot];
      }
    }
  }
  auto append=[&](const bo_batch_entry &entry){auto first=data.size();data.resize(first+40);std::memcpy(data.data()+first,&entry,160);};
  for(unsigned kind=0;kind<2;++kind)for(unsigned bank=0;bank<2;++bank){data[12+kind*2+bank]=data.size();
    for(unsigned j=0;j<masked.size();++j){unsigned i=masked[j];bo_batch_entry e{};
      if(!kind){e.kind=BO_BATCH_FILL;e.op.fill={3+bank,(header+plan.ids[i]*256)*4,1024,0,0};}
      else{e.kind=BO_BATCH_BLEND;auto &b=e.op.blend;b.bank=bank;b.tile=plan.ids[i];b.scratch_word=header+j*256;
        std::copy_n(plan.packed_masks[i].data(),32,b.mask);}
      append(e);
    }
  }
  data[16]=data.size();data.resize(data.size()+tiles,0xffffffffu);
  for(unsigned i=0;i<plan.ids.size();++i)if(plan.kinds[i])data[data[16]+plan.ids[i]]=plan.ids[i]*1024;
  data[2]=data.size();return data;
}
namespace {
thread_local uint64_t loads=0,hits=0,misses=0,rejected=0,builtin_loads=0,payload_scan_bytes=0;
struct Entry {std::string key;std::shared_ptr<const ProjectionPlan> plan;};
thread_local std::list<Entry> cache;
uint32_t word(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint32_t crc(const uint8_t *p,size_t n,uint32_t c=~0u){
  static const auto table=[] {std::array<uint32_t,256> t{};for(unsigned i=0;i<256;++i){uint32_t v=i;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320u:0);t[i]=v;}return t;}();
  for(size_t i=0;i<n;++i)c=table[(c^p[i])&255]^(c>>8);return c;
}
std::shared_ptr<const ProjectionPlan> read_plan(const std::string &path,const std::string &key,unsigned width,unsigned height,bool builtin){
  int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return {};
  struct Close {int fd;~Close(){close(fd);}} closer{fd};
  std::array<uint8_t,4096> h{};struct stat st{};
  require(!fstat(fd,&st) && S_ISREG(st.st_mode) && pread(fd,h.data(),h.size(),0)==ssize_t(h.size()),"plan header");
  require(std::equal(h.begin(),h.begin()+8,"BOGPLN02"),"plan format");
  unsigned n=word(h.data()+8),bytes=word(h.data()+12),k=word(h.data()+16);
  require(n && n<=tiles && bytes>=n*84 && bytes<=tiles*(84+1152) && !(bytes&3) &&
          k==key.size() && k<=4096-28 && uint64_t(st.st_size)==4096ull+bytes,"plan bounds");
  require(std::equal(key.begin(),key.end(),h.begin()+28) &&
          ~crc(h.data()+28,k,crc(h.data(),24))==word(h.data()+24),"plan metadata CRC/key/revision");
  void *mapped=mmap(nullptr,bytes,PROT_READ,MAP_PRIVATE,fd,4096);require(mapped!=MAP_FAILED,"plan mmap");
  std::shared_ptr<const Texture> storage;
  try{storage=std::make_shared<Texture>(static_cast<const uint32_t *>(mapped),bytes/4);}
  catch(...){munmap(mapped,bytes);throw;}
  auto body=static_cast<const uint8_t *>(mapped);
  // Immutable initramfs plans are exhaustively checked by the image build.
  // External /data and BOAYO_PLAN_DIR plans retain full validation.
  if(!builtin){payload_scan_bytes+=bytes;require(~crc(body,bytes)==word(h.data()+20),"plan payload CRC");}
  else ++builtin_loads;
  auto plan=std::make_shared<ProjectionPlan>();plan->storage=std::move(storage);
  plan->payload_crc=word(h.data()+20);
  plan->ids.reserve(n);plan->kinds.reserve(n);plan->descriptors.reserve(n);plan->masks.reserve(n);plan->packed_masks.reserve(n);
  unsigned cursor=n*84;
  for(unsigned i=0;i<n;++i){auto record=body+i*84;
    unsigned id=word(record),kind=word(record+4),offset=word(record+8);
    require(id<tiles && (!i || id>plan->ids.back()) && kind<=2,"plan tile order/type");
    Descriptor d{};for(unsigned j=0;j<18;++j)d[j]=word(record+12+j*4);
    require(d[0]==0 && d[1]==id*256 && d[2]==width && d[3]==height && d[4]==width && d[5]==1,"plan descriptor bounds");
    const uint8_t *mask=nullptr;std::array<uint32_t,32> packed{};
    if(kind==2){require(offset==cursor && bytes-cursor>=1152,"plan mask offset");mask=body+offset;
      if(!builtin){bool zero=false,one=false;for(unsigned j=0;j<1024;++j){require(mask[j]<=1,"plan mask value");zero|=mask[j]==0;one|=mask[j]==1;}
        require(zero && one,"plan mask classification");}
      for(unsigned j=0;j<32;++j)packed[j]=word(mask+1024+j*4);
      if(!builtin)for(unsigned j=0;j<1024;++j)require(((packed[j/32]>>(j%32))&1)==mask[j],"plan packed mask mismatch");
      cursor+=1152;
    }else require(!offset,"plan uniform mask offset");
    plan->ids.push_back(id);plan->kinds.push_back(kind);plan->descriptors.push_back(d);plan->masks.push_back(mask);plan->packed_masks.push_back(packed);
  }
  require(cursor==bytes,"plan trailing data");
  // Only the immutable initramfs recipe skips exhaustive checks. External files
  // must exactly match commands regenerated from the validated geometry plan.
  const char *ready_mode=getenv("BOAYO_READY_PLANS");
  if(!ready_mode || std::string(ready_mode)!="0"){
    int rfd=open((path+".ready").c_str(),O_RDONLY|O_CLOEXEC);
    if(rfd>=0){Close ready_close{rfd};struct stat rs{};
      require(!fstat(rfd,&rs)&&S_ISREG(rs.st_mode)&&rs.st_size>=72 && rs.st_size<=262144 && !(rs.st_size&3),"plan ready size");
      void *ptr=mmap(nullptr,rs.st_size,PROT_READ,MAP_PRIVATE,rfd,0);require(ptr!=MAP_FAILED,"plan ready mmap");
      std::shared_ptr<const Texture> ready;
      try{ready=std::make_shared<Texture>(static_cast<const uint32_t *>(ptr),rs.st_size/4);}catch(...){munmap(ptr,rs.st_size);throw;}
      auto r=ready->data();unsigned jobs=r[4]+r[5]+r[6]+r[7],masked=r[6]+r[7];
      require(r[0]==0x31594452 && r[1]==1 && r[2]==ready->size() && r[3]==plan->payload_crc && !r[17] && jobs<=128 &&
        r[4]<=64 && r[5]<=64 && r[6]<=64 && r[7]<=64,"plan ready header");
      unsigned end=18;
      for(unsigned i=0;i<4;++i){require(r[8+i]==end,"plan ready queue offset");end+=r[4+i]*18;}
      for(unsigned i=0;i<4;++i){require(r[12+i]==end,"plan ready batch offset");end+=masked*40;}
      require(r[16]==end && end+tiles==ready->size(),"plan ready directory bounds");
      if(!builtin){auto expected=make_device_plan(*plan);require(expected.size()==ready->size() && std::equal(expected.begin(),expected.end(),r),"plan ready commands mismatch");}
      plan->ready=std::move(ready);
    }
  }
  return plan;
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
  for(auto &d:dirs){try{auto plan=read_plan(d+name,key,geometry[0],geometry[1],!dir && d=="/usr/share/boayo/plans");if(!plan)continue;
    ++loads;cache.push_front({key,plan});size_t bytes=0;for(auto i=cache.begin();i!=cache.end();){bytes+=i->plan->storage->size()*4;
      if(bytes>16u*1024*1024 && i!=cache.begin())i=cache.erase(i);else ++i;}return plan;
  }catch(const std::runtime_error &e){if(std::string(e.what()).find("plan ")!=0)throw;++rejected;std::fprintf(stderr,"BOAYO_PLAN_REJECT %s: %s\n",name,e.what());}}
  ++misses;return {};
}
J projection_plan_stats(){return {{"builtin_loads",builtin_loads},{"payload_scan_bytes",payload_scan_bytes},{"loads",loads},{"hits",hits},{"misses",misses},{"rejected",rejected},{"revision",BOAYO_PLAN_REV}};}
}
