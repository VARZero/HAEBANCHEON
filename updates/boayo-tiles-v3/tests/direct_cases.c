static struct bosio_device dev;
static unsigned char user[0x400000+8192] __attribute__((aligned(4096)));
static void setup(void){memset(&dev,0,sizeof(dev));memset(arena,0xa5,sizeof(arena));
 dev.channel=&channel;dev.dma=0xbd000000;dev.arena=arena;dev.engine.active=-1;
 pins=unpins=dirty_pins=maps=unmaps=frees=issued=completed=submit_count=0;
 outstanding=mode=map_failure=prepare_failure=submit_failure=fail_at=pin_failure=access_failure=0;
 expected_pin_flags=FOLL_LONGTERM;allow_user_dma=0;
}
int main(void){
 for(unsigned i=0;i<sizeof(user);i++)user[i]=(i*13+17)&255;
 unsigned sizes[]={4,4092,4096,4100,921600,0x400000},offsets[]={0,4,4092};
 for(int merge=0;merge<2;merge++)for(unsigned a=0;a<6;a++)for(unsigned o=0;o<3;o++){
  setup();merged_pages=merge;struct bo_upload u={.region=BO_TEXTURE,.bytes=sizes[a],.data=(uintptr_t)(user+offsets[o])};
  assert(!bosio_direct_user(&dev,&u));assert(!memcmp(arena+bo_offsets[BO_TEXTURE],user+offsets[o],u.bytes));
  assert(arena[bo_offsets[BO_TEXTURE]+u.bytes]==0xa5 && pins==unpins && !dirty_pins && maps==unmaps && !dev.held_direct);
 }
 merged_pages=0;setup();struct bo_upload u={.region=BO_TEXTURE,.bytes=16384,.data=(uintptr_t)(user+4)};
 u.reserved=1;assert(bosio_direct_user(&dev,&u)==-EINVAL);u.reserved=0;
 u.data++;assert(bosio_direct_user(&dev,&u)==-EINVAL);u.data--;
 u.bytes=3;assert(bosio_direct_user(&dev,&u)==-EINVAL);u.bytes=16384;
 u.region=BO_REGIONS;assert(bosio_direct_user(&dev,&u)==-EINVAL);u.region=BO_TEXTURE;
 access_failure=1;assert(bosio_direct_user(&dev,&u)==-EFAULT);access_failure=0;
 pin_failure=1;assert(bosio_direct_user(&dev,&u)==-EFAULT && pins==unpins && !issued);
 setup();map_failure=1;assert(bosio_direct_user(&dev,&u)==-EIO && pins==unpins && !issued);
 for(int fault=0;fault<4;fault++){
  setup();fail_at=2;prepare_failure=fault==0;submit_failure=fault==1;mode=fault==2?2:fault==3?1:0;
  assert(bosio_direct_user(&dev,&u)==(fault==0?-ENOMEM:fault==3?-ETIMEDOUT:-EIO));
  assert(dev.engine.poisoned);
  if(fault==3){assert(outstanding && pins && !unpins && !unmaps && dev.held_direct);
   assert(bosio_direct_user(&dev,&u)==-EIO);finish_dma(0);assert(!unpins);
   bosio_direct_release(&dev,dev.held_direct);dev.held_direct=NULL;
  }else assert(!outstanding && pins==unpins && maps==unmaps && !dev.held_direct);
 }
 puts("PASS DIRECT actual-driver source-page DMA: 36 size/alignment/SG cases, no FOLL_WRITE/dirty pages, invalid/pin/map/partial/error/timeout/late-read lifetime");
}


