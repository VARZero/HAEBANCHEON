#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../software/bosio/common/bosio_batch.h"
static unsigned char arena[BO_ARENA_BYTES];
static unsigned transfers,reads,writes;static int fail;
static int transfer(void *p,__u32 dst,__u32 src,__u32 bytes,int fill,__u32 color){
 (void)p;assert(bytes&&!(bytes&3)&&dst+bytes<=sizeof(arena)&&src+bytes<=sizeof(arena));
 ++transfers;if(fail && transfers==(unsigned)fail)return -ETIMEDOUT;
 if(fill)memset(arena+dst,color,bytes);else memcpy(arena+dst,arena+src,bytes);return 0;
}
static __u32 read_word(void *p,__u32 a){__u32 v;(void)p;++reads;assert(!(a&3));memcpy(&v,arena+a,4);return v;}
static void write_word(void *p,__u32 a,__u32 v){(void)p;++writes;assert(!(a&3));memcpy(arena+a,&v,4);}
static void fence(void *p){(void)p;}
int main(void){
 struct bo_engine e={.active=0};struct bo_tile_io io={0,transfer,read_word,write_word};
 struct bo_batch_entry b[BO_BATCH_MAX]={0};unsigned i;
 __u32 dst=bo_offsets[BO_SCENE1]+BO_HEADER_WORDS*4;
 e.io.memory_fence=fence;
 b[0].kind=BO_BATCH_FILL;b[0].op.fill=(struct bo_mem_fill){BO_SCENE1,BO_HEADER_WORDS*4,1024,0x31,0};
 b[1].kind=BO_BATCH_COPY;b[1].op.copy=(struct bo_mem_copy){BO_SCENE1,BO_HEADER_WORDS*4,BO_SCRATCH,BO_HEADER_WORDS*4,1024,0};
 b[2].kind=BO_BATCH_BLEND;b[2].op.blend=(struct bo_tile_blend){.bank=1,.tile=0,.mode=1,.color=0x62};
 b[2].op.blend.mask[0]=0x13;
 b[3].kind=BO_BATCH_BLEND;b[3].op.blend=(struct bo_tile_blend){.bank=1,.tile=1,.scratch_word=BO_HEADER_WORDS};
 memset(b[3].op.blend.mask,255,sizeof(b[3].op.blend.mask));
 assert(!bo_batch_apply(&e,b,4,&io));
 for(i=0;i<1024;i++)assert(arena[dst+i]==((i==0||i==1||i==4)?0x62:0x31));
 for(i=0;i<1024;i++)assert(arena[dst+1024+i]==0x31);
 /* Invalid last entry must prevent even the first fill from executing. */
 memset(arena+dst,0xa5,2048);transfers=reads=writes=0;
 b[3].op.blend.bank=0;assert(bo_batch_apply(&e,b,4,&io)==-EBUSY);
 assert(!transfers&&!reads&&!writes&&!e.poisoned&&arena[dst]==0xa5);b[3].op.blend.bank=1;
 b[3].reserved=1;assert(bo_batch_apply(&e,b,4,&io)==-EINVAL);b[3].reserved=0;
 b[3].kind=99;assert(bo_batch_apply(&e,b,4,&io)==-EINVAL);b[3].kind=BO_BATCH_BLEND;
 assert(bo_batch_apply(&e,b,0,&io)==-EINVAL);
 assert(bo_batch_apply(&e,b,BO_BATCH_MAX+1,&io)==-EINVAL);
 assert(!transfers&&!writes);
 fail=2;assert(bo_batch_apply(&e,b,4,&io)==-ETIMEDOUT&&e.poisoned);
 assert(transfers==2&&arena[dst]==0x31&&arena[dst+1024]==0xa5);
 assert(bo_batch_apply(&e,b,4,&io)==-EIO&&transfers==2);
 e.poisoned=0;fail=0;
 for(i=0;i<BO_BATCH_MAX;i++)b[i]=b[0];
 assert(!bo_batch_apply(&e,b,BO_BATCH_MAX,&io));
 puts("PASS BATCH actual-kernel helper ordered fill/copy/blend, all-entry validation, 128 limit, active-bank exclusion, partial failure poison");
}
