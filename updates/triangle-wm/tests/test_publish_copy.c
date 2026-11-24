#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bosio_engine.h"
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
static __u32 banks[2][BO_HEADER_WORDS+4];
static unsigned upload_count,uploaded;static int upload_fail;
static int upload(void *opaque,unsigned region,__u32 offset,const __u32 *data,__u32 words){
 (void)opaque;assert(region==BO_SCENE0||region==BO_SCENE1);assert(offset+words<=BO_HEADER_WORDS);
 upload_count++;if(upload_fail)return -ETIMEDOUT;
 memcpy(banks[region-BO_SCENE0]+offset,data,words*4);uploaded+=words;return 0;
}
int main(void){
 struct hw h={0};struct bo_engine e;struct bo_io io={&h,read_reg,write_reg,now,delay,fence};
 __u32 metadata[BO_HEADER_WORDS];unsigned i,before;
 struct bo_present p={.scene_words=1084800,.resolution=2,.timeout_ms=1};
 for(i=0;i<BO_HEADER_WORDS;i++)metadata[i]=i<256?i:0xffffffffu;
 memset(banks,0xa5,sizeof(banks));bo_init(&e,&io,0xbd000000);
 assert(!bo_publish(&e,&p,metadata,upload,0));assert(h.command==1&&e.active==0&&uploaded==BO_HEADER_WORDS);
 assert(!memcmp(banks[0],metadata,sizeof(metadata))&&banks[0][BO_HEADER_WORDS]==0xa5a5a5a5);
 p.bank=1;assert(!bo_publish(&e,&p,metadata,upload,0));assert(h.command==2&&upload_count==2);
 p.bank=0;before=upload_count;assert(!bo_publish(&e,&p,metadata,upload,0));
 assert(upload_count==before&&h.command==2&&e.profile.uploaded_words==0);
 p.bank=1;metadata[256]=0;assert(!bo_publish(&e,&p,metadata,upload,0));
 assert(e.profile.uploaded_words==1&&e.profile.upload_ranges==1&&h.command==1);
 /* A legacy header write invalidates the target snapshot; no stale skip. */
 assert(!bo_upload_check(&e,BO_SCENE0,0,4));banks[0][0]=0xdeadbeef;
 p.bank=0;assert(!bo_publish(&e,&p,metadata,upload,0));assert(e.profile.uploaded_words==BO_HEADER_WORDS&&banks[0][0]==metadata[0]);
 p.bank=1;metadata[0]^=1;assert(!bo_publish(&e,&p,metadata,upload,0));assert(e.profile.uploaded_words==1&&h.command==1);
 before=upload_count;p.bank=0;metadata[256]=0xfffffffe;
 assert(bo_publish(&e,&p,metadata,upload,0)==-EINVAL&&upload_count==before&&!e.poisoned);
 metadata[256]=0;p.bank=1;assert(bo_publish(&e,&p,metadata,upload,0)==-EBUSY&&upload_count==before);
 p.bank=0;p.camera[0]=1;assert(bo_publish(&e,&p,metadata,upload,0)==-EBUSY&&upload_count==before);p.camera[0]=0;
 /* Trusted copy always uploads every metadata word and commands a full load. */
 before=uploaded;p.bank=0;
 assert(!bo_publish_copy(&e,&p,metadata,upload,0));
 assert(uploaded-before==BO_HEADER_WORDS && h.command==1 && e.active==0);
 assert(!e.bank_metadata_valid[0] && !e.metadata_valid);
 assert(!memcmp(banks[0],metadata,sizeof(metadata)));
 p.bank=1;before=uploaded;assert(!bo_publish_copy(&e,&p,metadata,upload,0));
 assert(uploaded-before==BO_HEADER_WORDS && h.command==1);
 /* Checked API after trusted copy must rebuild target metadata, not skip stale bytes. */
 p.bank=0;before=uploaded;assert(!bo_publish(&e,&p,metadata,upload,0));
 assert(uploaded-before==BO_HEADER_WORDS);
 p.bank=1;before=upload_count;p.scene_words=4479;
 assert(bo_publish_copy(&e,&p,metadata,upload,0)==-EINVAL && upload_count==before);
 p.scene_words=1084800;p.bank=0;
 assert(bo_publish_copy(&e,&p,metadata,upload,0)==-EBUSY && upload_count==before);
 p.bank=1;p.camera[0]=1;
 assert(bo_publish_copy(&e,&p,metadata,upload,0)==-EBUSY && upload_count==before);p.camera[0]=0;
 upload_fail=1;assert(bo_publish_copy(&e,&p,metadata,upload,0)==-ETIMEDOUT && e.poisoned);
 assert(!e.bank_metadata_valid[1]);upload_fail=0;
 bo_init(&e,&io,0xbd000000);h=(struct hw){0};p.bank=1;
 assert(!bo_publish(&e,&p,metadata,upload,0));p.bank=0;
 upload_fail=1;assert(bo_publish(&e,&p,metadata,upload,0)==-ETIMEDOUT&&e.poisoned&&e.active==1);
 assert(!e.bank_metadata_valid[0]);assert(bo_publish(&e,&p,metadata,upload,0)==-EIO);

 bo_init(&e,&io,0xbd000000);h=(struct hw){0};upload_fail=0;p.bank=0;
 assert(!bo_publish(&e,&p,metadata,upload,0));p.bank=1;h.freeze=1;
 assert(bo_publish(&e,&p,metadata,upload,0)==-ETIMEDOUT&&e.poisoned&&e.active==0);
 h.freeze=0;assert(bo_publish(&e,&p,metadata,upload,0)==-EIO);
 puts("PASS PUBLISH_COPY and checked engine: atomic snapshot validation, exact dirty ranges, zero DDR read interface, legacy invalidation, no cell writes, full/patch, upload/ack timeout poison");
}
