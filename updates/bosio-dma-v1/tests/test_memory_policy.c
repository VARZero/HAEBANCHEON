#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../software/bosio/common/bosio_engine.h"
int main(void)
{
 struct bo_engine e={.active=0};
 struct bo_mem_copy c={BO_SCENE0,0,BO_SCENE1,0,4096,0};
 struct bo_mem_fill f={BO_SCRATCH,0,BO_SCRATCH_BYTES,0,0};
 assert(sizeof(struct bo_dma_info)==72 && sizeof(c)==24 && sizeof(f)==20);
 assert(!bo_copy_check(&e,&c)); /* read active, write inactive */
 c.dst_region=BO_SCENE0;assert(bo_copy_check(&e,&c)==-EBUSY);
 c.dst_region=BO_SCRATCH;c.src_region=BO_SCRATCH;c.src_offset=0;c.dst_offset=4;
 assert(bo_copy_check(&e,&c)==-EINVAL);
 c.dst_offset=4096;assert(!bo_copy_check(&e,&c));
 c.src_offset=0xfffffffc;assert(bo_copy_check(&e,&c)==-EINVAL);
 c.src_offset=0;c.dst_offset=BO_SCRATCH_BYTES-4096;assert(!bo_copy_check(&e,&c));
 c.dst_offset+=4;assert(bo_copy_check(&e,&c)==-EINVAL);
 c.dst_offset=4096;c.bytes=0;assert(bo_copy_check(&e,&c)==-EINVAL);
 c.bytes=4096;c.src_offset=1;assert(bo_copy_check(&e,&c)==-EINVAL);
 c.src_offset=0;c.reserved=1;assert(bo_copy_check(&e,&c)==-EINVAL);
 assert(!bo_fill_check(&e,&f));f.value=255;assert(!bo_fill_check(&e,&f));
 f.value=256;assert(bo_fill_check(&e,&f)==-EINVAL);f.value=0;
 f.region=BO_SCENE0;assert(bo_fill_check(&e,&f)==-EBUSY);
 f.region=BO_SCRATCH;f.offset=4;assert(bo_fill_check(&e,&f)==-EINVAL);f.offset=0;
 e.poisoned=1;assert(bo_fill_check(&e,&f)==-EIO);
 c.reserved=0;assert(bo_copy_check(&e,&c)==-EIO);
 puts("PASS memory policy: ABI, active destination, active source, overlap, bounds, alignment, poison");
}
