static struct bosio_device dev;
static void setup(void)
{
 memset(&dev,0,sizeof(dev));memset(arena,0xa5,sizeof(arena));
 dev.channel=&channel;dev.dma=0xbd000000;dev.arena=arena;dev.engine.active=-1;
 maps=unmaps=frees=issued=completed=submit_count=0;outstanding=0;
 mode=vmalloc_mode=map_failure=prepare_failure=submit_failure=fail_at=0;
}
static void *source(unsigned n,int vm)
{
 void *p=NULL;assert(!posix_memalign(&p,vm?4096:64,n));
 for(unsigned i=0;i<n;i++)((unsigned char*)p)[i]=(i*37+(i>>12)*13)&255;
 return p;
}
int main(void)
{
 for(int vm=0;vm<2;vm++)for(unsigned n=4096;n<=65536;n*=2) {
  setup();vmalloc_mode=vm;void *p=source(n,vm);unsigned char *expect=malloc(n);memcpy(expect,p,n);
  assert(!bosio_upload_dma(&dev,p,0xbd010000,n));
  assert(!memcmp(arena+0x10000,expect,n));free(expect);
  assert(!dev.held_upload && !dev.engine.poisoned && maps==1 && unmaps==1 && frees==1);
  assert(dev.dma_info.upload_bytes==n && dev.dma_info.upload_ops==1 && completed==issued);
 }
 setup();assert(!bosio_mem_transfer(&dev,0xbd010000,0,8192,1,0x39));
 assert(arena[0x10000]==0x39 && arena[0x11fff]==0x39 && arena[0x12000]==0xa5);
 assert(!bosio_mem_transfer(&dev,0xbd020000,0xbd010000,8192,0,0));
 assert(!memcmp(arena+0x10000,arena+0x20000,8192));
 setup();map_failure=1;assert(bosio_upload_dma(&dev,source(8192,0),0xbd010000,8192)==-EIO);
 assert(!issued && !dev.held_upload && !dev.engine.poisoned && frees==1);
 setup();prepare_failure=1;assert(bosio_upload_dma(&dev,source(8192,0),0xbd010000,8192)==-ENOMEM);
 assert(!issued && unmaps==1 && frees==1 && !dev.engine.poisoned);
 setup();submit_failure=1;assert(bosio_upload_dma(&dev,source(8192,0),0xbd010000,8192)==-EIO);
 assert(!issued && unmaps==1 && frees==1 && !dev.engine.poisoned);
 setup();mode=2;assert(bosio_upload_dma(&dev,source(8192,0),0xbd010000,8192)==-EIO);
 assert(dev.engine.poisoned && unmaps==1 && frees==1 && !outstanding);
 setup();mode=1;void *p=source(8192,0);
 assert(bosio_upload_dma(&dev,p,0xbd010000,8192)==-ETIMEDOUT);
 assert(dev.engine.poisoned && dev.dma_wait.quarantined && dev.held_upload);
 assert(!unmaps && !frees && outstanding && issued==1);
 assert(bosio_mem_transfer(&dev,0xbd020000,0,4096,1,0)==-EIO && issued==1);
 finish_dma(0); /* ASan catches freed source/callback state on a late completion. */
 assert(dev.dma_wait.quarantined && dev.engine.poisoned && !unmaps && !frees);
 /* Test teardown only after simulated late completion, unlike driver reset policy. */
 bosio_unmap_upload(&dev,dev.held_upload);dev.held_upload=NULL;
 for(int fault=0;fault<4;fault++) {
  setup();vmalloc_mode=1;fail_at=2;
  prepare_failure=fault==0;submit_failure=fault==1;mode=fault==2?2:fault==3?1:0;
  void *partial=source(12288,1);
  int ret=bosio_upload_dma(&dev,partial,0xbd010000,12288);
  assert(ret==(fault==0?-ENOMEM:fault==3?-ETIMEDOUT:-EIO));
  assert(dev.engine.poisoned && dev.dma_info.copy_bytes==4096);
  assert(dev.dma_info.upload_ops==0 && dev.dma_info.upload_bytes==0);
  for(unsigned i=0;i<4096;i++)assert(arena[0x10000+i]==((i*37)&255));
  assert(arena[0x12000]==0xa5); /* third segment never submitted */
  if(fault==3) {
   assert(outstanding && !unmaps && !frees && dev.held_upload);
   finish_dma(0);
   assert(dev.engine.poisoned && dev.dma_wait.quarantined);
   bosio_unmap_upload(&dev,dev.held_upload);dev.held_upload=NULL;
  } else assert(!outstanding && unmaps==1 && frees==1 && !dev.held_upload);
 }
 puts("PASS actual DMA wait/upload functions: kmalloc/vmalloc SG, fill/copy, mapping/prep/submit/completion errors, partial SG failure, timeout pinning and late completion");
}
