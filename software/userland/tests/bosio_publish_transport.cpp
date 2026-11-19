#include "backend.hpp"
#include <sys/ioctl.h>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
using namespace boayo;
static unsigned signature=0x42533235u,expected_bank=0,calls=0;
static bool expect_tag=true,inject_error=false;
static Words original;
extern "C" int ioctl(int,unsigned long command,...) noexcept {
 va_list args;va_start(args,command);void *arg=va_arg(args,void*);va_end(args);
 if(command==BO_GET_INFO){auto *info=static_cast<bo_info*>(arg);*info={};info->abi=3;info->signature=signature;info->active_bank=-1;return 0;}
 if(command==BO_GET_DMA_INFO){auto *info=static_cast<bo_dma_info*>(arg);*info={};info->version=1;info->features=BO_DMA_PUBLISH;return 0;}
 if(command==BO_STOP)return 0;
 if(command==BO_PUBLISH){auto &p=*static_cast<bo_publish*>(arg);auto words=reinterpret_cast<const uint32_t*>(uintptr_t(p.metadata));
  if(p.words!=header || p.present.bank!=expected_bank)return errno=EINVAL,-1;
  uint32_t first=expect_tag?(original[0]&0x00ffffffu)|((expected_bank+1)<<24):original[0];
  if(words[0]!=first || !std::equal(original.begin()+1,original.begin()+header,words+1))return errno=EINVAL,-1;
  ++calls;if(inject_error)return errno=EIO,-1;p.profile.uploaded_words=1;return 0;
 }
 return errno=ENOTTY,-1;
}
int main(){try{
 Words metadata(header);for(unsigned i=0;i<header;++i)metadata[i]=i*2654435761u;metadata[0]=0xd0123456;
 original=metadata;std::array<int32_t,180> camera{};unsetenv("BOSIO_METADATA_BANK_TAG");
 {LinuxDevice device("/dev/null");for(unsigned n=0;n<8;++n){expected_bank=n%2;expect_tag=true;
   require(device.publish(expected_bank,scene_words,2,camera,metadata)==1,"publish return");require(metadata==original,"logical metadata changed");}
  setenv("BOSIO_METADATA_BANK_TAG","0",1);expect_tag=false;device.publish(expected_bank,scene_words,2,camera,metadata);
  require(metadata==original,"fallback modified metadata");unsetenv("BOSIO_METADATA_BANK_TAG");
  expect_tag=true;inject_error=true;bool failed=false;try{device.publish(expected_bank,scene_words,2,camera,metadata);}catch(...){failed=true;}
  require(failed && metadata==original,"failed publication modified metadata");inject_error=false;
 }
 signature=0xdeadbeef;expect_tag=false;
 {LinuxDevice other("/dev/null");other.publish(expected_bank,scene_words,2,camera,metadata);require(metadata==original,"other ABI3 transport changed metadata");}
 require(calls==11,"missing transport cases");std::puts("PASS_LINUX_PUBLISH_TRANSPORT tags/banks/RGB24/logical-snapshot/fallback/other-signature/failure");return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
