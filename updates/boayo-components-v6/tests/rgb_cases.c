static struct bosio_device dev;
static unsigned char pattern[BO_RGB_PATTERN_BYTES] __attribute__((aligned(4096)));
static unsigned char user[BO_RGB_MAX_BYTES+8192] __attribute__((aligned(4096)));
static void setup(void)
{
 memset(&dev,0,sizeof(dev));memset(user,0xa5,sizeof(user));
 dev.channel=&channel;dev.dma=0xbd000000;dev.arena=arena;
 dev.rgb_pattern=pattern;dev.rgb_dma=(uintptr_t)pattern;
 pins=unpins=dirty_pins=maps=unmaps=frees=issued=completed=submit_count=0;
 outstanding=mode=map_failure=prepare_failure=submit_failure=fail_at=pin_failure=access_failure=0;
 allow_user_dma=1;
}
static struct bo_rgb_fill request(unsigned off,unsigned n,unsigned color)
{return (struct bo_rgb_fill){.data=(uintptr_t)(user+off),.bytes=n,.color=color};}
int main(void)
{
 unsigned colors[]={0,0xffffff,0x080503,0x201810,0x0102fe};
 unsigned sizes[]={4,12,4092,4096,4100,12288,691200,BO_RGB_MAX_BYTES};
 unsigned offsets[]={0,4,8,12,4088,4092};
 for(int merge=0;merge<2;merge++)for(unsigned c=0;c<5;c++)for(unsigned s=0;s<8;s++)for(unsigned o=0;o<6;o++) {
  setup();merged_pages=merge;struct bo_rgb_fill f=request(offsets[o],sizes[s],colors[c]);
  assert(!bosio_rgb_user(&dev,&f));
  for(unsigned i=0;i<f.bytes;i++)assert(user[offsets[o]+i]==((f.color>>(8*(i%3)))&255));
  for(unsigned i=0;i<offsets[o];i++)assert(user[i]==0xa5);
  assert(user[offsets[o]+f.bytes]==0xa5 && pins==unpins && pins==dirty_pins && maps==unmaps);
  assert(!dev.held_rgb && !outstanding && dev.rgb_valid);
  unsigned before=issued;assert(!bosio_rgb_user(&dev,&f));
  assert(issued>before && issued-before<before); /* cached pattern skips seed expansion */
 }
 merged_pages=0;setup();struct bo_rgb_fill f=request(4,691200,0x080503);
 f.reserved[1]=1;assert(bosio_rgb_user(&dev,&f)==-EINVAL);f.reserved[1]=0;
 f.color=0x1000000;assert(bosio_rgb_user(&dev,&f)==-EINVAL);f.color=0x080503;
 f.bytes=3;assert(bosio_rgb_user(&dev,&f)==-EINVAL);f.bytes=691200;
 f.data++;assert(bosio_rgb_user(&dev,&f)==-EINVAL);f.data--;
 access_failure=1;assert(bosio_rgb_user(&dev,&f)==-EFAULT);access_failure=0;
 pin_failure=1;assert(bosio_rgb_user(&dev,&f)==-EFAULT);assert(pins==unpins && !issued);
 setup();f=request(4,8192,0x080503);map_failure=1;
 assert(bosio_rgb_user(&dev,&f)==-EIO && pins==unpins && !issued);
 for(int fault=0;fault<4;fault++) {
  setup();f=request(4,16384,0x080503);
  assert(!bosio_rgb_pattern(&dev,f.color));
  unsigned before=issued;fail_at=before+2;
  prepare_failure=fault==0;submit_failure=fault==1;mode=fault==2?2:fault==3?1:0;
  int ret=bosio_rgb_user(&dev,&f);
  assert(ret==(fault==0?-ENOMEM:fault==3?-ETIMEDOUT:-EIO));
  assert(dev.engine.poisoned);
  if(fault==3) {
   assert(outstanding && pins && !unpins && !unmaps && dev.held_rgb);
   assert(bosio_rgb_user(&dev,&f)==-EIO);
   finish_dma(0);assert(dev.dma_wait.quarantined && !unpins);
   bosio_rgb_release(&dev,dev.held_rgb);dev.held_rgb=NULL; /* test teardown only */
  } else assert(!outstanding && pins==unpins && maps==unmaps && !dev.held_rgb);
 }
 puts("PASS RGB DMA actual driver: 480 color/size/page-phase/scatter-merged cases, canaries, cached seed, invalid/pin/map/error/timeout/late-write lifetime model");
}
