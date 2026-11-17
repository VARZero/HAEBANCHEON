#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../software/bosio/common/bosio_tile.h"
static unsigned char arena[BO_ARENA_BYTES];
static unsigned transfers,reads,writes;static int fail;
static int transfer(void *p,__u32 dst,__u32 src,__u32 bytes,int fill,__u32 color){
 (void)p;assert(bytes&&!(bytes&3)&&dst+bytes<=sizeof(arena)&&src+bytes<=sizeof(arena));
 ++transfers;if(fail)return -ETIMEDOUT;
 if(fill)memset(arena+dst,color,bytes);else memcpy(arena+dst,arena+src,bytes);return 0;
}
static __u32 read_word(void *p,__u32 a){__u32 v;(void)p;++reads;assert(!(a&3));memcpy(&v,arena+a,4);return v;}
static void write_word(void *p,__u32 a,__u32 v){(void)p;++writes;assert(!(a&3));memcpy(arena+a,&v,4);}
static void fence(void *p){(void)p;}
static __u32 rng=7;
static __u32 next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 struct bo_engine e={.active=0};struct bo_tile_io io={0,transfer,read_word,write_word};
 unsigned k,i;unsigned char expected[1024];e.io.memory_fence=fence;
 for(k=0;k<1024;++k){struct bo_tile_blend b={.bank=1,.tile=k%4220,.scratch_word=BO_HEADER_WORDS,.mode=k%2,.color=k%256};
  if(b.mode)b.scratch_word=0;
  unsigned dst=bo_offsets[BO_SCENE1]+(BO_HEADER_WORDS+b.tile*256)*4,src=bo_offsets[BO_SCRATCH]+BO_HEADER_WORDS*4;
  memset(arena+dst-4,0x69,1032);
  for(i=0;i<1024;++i){arena[src+i]=next();expected[i]=0x69;}
  for(i=0;i<32;++i)b.mask[i]=k<2?~0u:k<4?0:next();
  for(i=0;i<1024;++i)if(b.mask[i/32]&(1u<<(i%32)))expected[i]=b.mode?b.color:arena[src+i];
  reads=writes=transfers=0;assert(!bo_tile_apply(&e,&b,&io));assert(!memcmp(arena+dst,expected,1024));
  for(i=0;i<4;++i)assert(arena[dst-4+i]==0x69&&arena[dst+1024+i]==0x69);
  if(k<2)assert(transfers==1&&!reads&&!writes);
  if(k>=2&&k<4)assert(!transfers&&!reads&&!writes);
 }
 struct bo_tile_blend b={.bank=0,.tile=0,.scratch_word=BO_HEADER_WORDS};
 memset(b.mask,255,sizeof(b.mask));assert(bo_tile_apply(&e,&b,&io)==-EBUSY);
 b.bank=2;assert(bo_tile_apply(&e,&b,&io)==-EINVAL);b.bank=1;
 b.tile=4220;assert(bo_tile_apply(&e,&b,&io)==-EINVAL);b.tile=0;
 b.scratch_word=BO_SCRATCH_BYTES/4-255;assert(bo_tile_apply(&e,&b,&io)==-EINVAL);b.scratch_word=BO_HEADER_WORDS;
 b.reserved=1;assert(bo_tile_apply(&e,&b,&io)==-EINVAL);b.reserved=0;
 b.mode=2;assert(bo_tile_apply(&e,&b,&io)==-EINVAL);b.mode=0;
 fail=1;assert(bo_tile_apply(&e,&b,&io)==-ETIMEDOUT&&e.poisoned);
 assert(bo_tile_apply(&e,&b,&io)==-EIO);
 puts("PASS TILE actual-kernel helper 1024 masks/paint/copy, stale data, guards, active-bank exclusion, timeout poison");
}
