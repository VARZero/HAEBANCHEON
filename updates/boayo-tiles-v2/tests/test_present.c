#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../software/bosio/common/bosio_engine.h"
struct hw {unsigned enabled,pending,base,active,command,loads,patches,freeze,error;__u64 time;};
static int read_reg(void *v,unsigned c,__u32 a,__u32 *x){
 struct hw *h=v;(void)c;*x=0;
 if(a==0x40)*x=1;
 if(a==0x44)*x=h->active;
 if(a==4){
  if(h->enabled && h->pending && !h->freeze && !h->error){h->active=h->base;h->pending=0;}
  *x=(h->enabled?3:0)|(h->pending?16:0)|(h->error?32:0);
 }
 return 0;
}
static int write_reg(void *v,unsigned c,__u32 a,__u32 x){
 struct hw *h=v;(void)c;
 if(a==0)h->enabled=x;
 if(a==8)h->base=x;
 if(a==0x6c){h->command=x;h->pending=1;if(x==1)h->loads++;else {assert(x==2);h->patches++;}}
 return 0;
}
static __u64 now(void *v){return ((struct hw*)v)->time;}
static void delay(void *v,unsigned n){((struct hw*)v)->time+=n;}
static void fence(void *v){(void)v;}
int main(void){
 struct hw h={0};struct bo_engine e;struct bo_io io={&h,read_reg,write_reg,now,delay,fence};
 /* Metadata only: ASan catches any accidental inspection of cell words. */
 __u32 scene[BO_HEADER_WORDS];unsigned i;
 struct bo_present p={.scene_words=1084800,.resolution=2,.timeout_ms=1};
 for(i=0;i<BO_HEADER_WORDS;i++)scene[i]=i<256?i:0xffffffffu;
 bo_init(&e,&io,0xbd000000);
 assert(!bo_present(&e,&p,scene)&&h.command==1);
 p.bank=1;assert(!bo_present(&e,&p,scene)&&h.command==2);
 p.bank=0;scene[256]=0;assert(!bo_present(&e,&p,scene)&&h.command==1);
 p.bank=1;assert(!bo_present(&e,&p,scene)&&h.command==2);
 p.bank=0;scene[12]^=1;assert(!bo_present(&e,&p,scene)&&h.command==1);
 p.bank=1;p.scene_words-=16;assert(!bo_present(&e,&p,scene)&&h.command==1);
 p.bank=0;scene[256]=0xffffffffu;assert(!bo_present(&e,&p,scene)&&h.command==1);
 assert(bo_present(&e,&p,scene)==-EBUSY);
 p.bank=1;h.freeze=1;
 assert(bo_present(&e,&p,scene)==-ETIMEDOUT && e.poisoned && e.active==0);
 assert(bo_upload_check(&e,BO_SCENE1,0,4)==-EIO);
 h.freeze=0;assert(bo_present(&e,&p,scene)==-EIO); /* no reuse after late completion */
 bo_init(&e,&io,0xbd000000);h=(struct hw){0};scene[256]=0xfffffffe;
 assert(bo_present(&e,&p,scene)==-EINVAL && h.loads==0);
 scene[256]=0xffffffffu;h.error=1;
 assert(bo_present(&e,&p,scene)==-EIO && e.poisoned);
 puts("PASS PRESENT actual engine: full/patch, palette/directory/length change, bounds, metadata-only reads, timeout and late completion quarantine");
}
