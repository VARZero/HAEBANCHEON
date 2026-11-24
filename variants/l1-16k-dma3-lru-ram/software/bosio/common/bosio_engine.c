#include "bosio_engine.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/sort.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#endif
const __u32 bo_offsets[BO_REGIONS] = {0x900000, 0, 0x2000, 0x10000, 0x450000, 0xd00000};
const __u32 bo_sizes[BO_REGIONS] = {0x400000, BO_MAX_JOBS*72, BO_MAX_JOBS*72,
                                  BO_MAX_WORDS*4, BO_MAX_WORDS*4, BO_SCRATCH_BYTES};
static int rd(struct bo_engine *e, unsigned int c, __u32 a, __u32 *v)
{ return e->io.read(e->io.opaque,c,a,v); }
static int wr(struct bo_engine *e, unsigned int c, __u32 a, __u32 v)
{ return e->io.write(e->io.opaque,c,a,v); }
static int poison(struct bo_engine *e, int error)
{ e->poisoned=1; return error; }
static int timeout_ok(__u32 ms) { return ms>=1 && ms<=30000; }
static int scene_ok(__u32 words) { return words>=4480 && words<=BO_MAX_WORDS && !(words&15); }
static __u64 deadline(struct bo_engine *e, __u32 ms)
{ return e->io.now_us(e->io.opaque)+(__u64)ms*1000; }
static int expired(struct bo_engine *e, __u64 end)
{ e->io.delay_us(e->io.opaque,10); return e->io.now_us(e->io.opaque)>=end; }
void bo_init(struct bo_engine *e, const struct bo_io *io, __u32 base)
{ memset(e,0,sizeof(*e)); e->io=*io;e->dma_base=base;e->active=-1; }
int bo_info(struct bo_engine *e, struct bo_info *v)
{
    memset(v,0,sizeof(*v));v->abi=BO_ABI;v->active_bank=e->active;
    v->poisoned=e->poisoned;v->max_jobs=BO_MAX_JOBS;v->max_scene_words=BO_MAX_WORDS;
    if(rd(e,2,0x7c,&v->signature)||rd(e,2,0x50,&v->capability)||
       rd(e,2,4,&v->status)||rd(e,2,0x44,&v->active_base)) return -EIO;
    return 0;
}
int bo_set_pose(struct bo_engine *e,const struct bo_pose *p)
{
    __u32 status;__u64 end;unsigned n;
    if(e->poisoned)return -EIO;
    if(!timeout_ok(p->timeout_ms))return -EINVAL;
    if(!e->configured || e->active<0)return -EINVAL;
    end=deadline(e,p->timeout_ms);
    if(rd(e,2,4,&status))return poison(e,-EIO);
    if((status&3)!=3 || (status&32))return poison(e,-EIO);
    /* Never overwrite a shadow bank that still belongs to an earlier commit. */
    while(status&8){
        if(expired(e,end))return poison(e,-ETIMEDOUT);
        if(rd(e,2,4,&status))return poison(e,-EIO);
        if(status&32)return poison(e,-EIO);
    }
    if(wr(e,2,0x78,0)||wr(e,2,0x60,0))return poison(e,-EIO);
    for(n=0;n<180;n++)if(wr(e,2,0x64,p->camera[n]))return poison(e,-EIO);
    if(wr(e,2,0x68,1))return poison(e,-EIO);
    /* AXI write acceptance is not completion: cfg_ack clears pending at a frame boundary. */
    for(;;){
        if(rd(e,2,4,&status))return poison(e,-EIO);
        if(status&32)return poison(e,-EIO);
        if(!(status&8))break;
        if(expired(e,end))return poison(e,-ETIMEDOUT);
    }
    memcpy(e->camera,p->camera,sizeof(e->camera));return 0;
}
int bo_upload_check(struct bo_engine *e,unsigned int region,__u32 offset,__u32 bytes)
{
    if(e->poisoned)return -EIO;
    if(region>=BO_REGIONS || (offset|bytes)&3 || !bytes || offset>bo_sizes[region] || bytes>bo_sizes[region]-offset)return -EINVAL;
    if(region>=BO_SCENE0 && region<=BO_SCENE1 && (int)(region-BO_SCENE0)==e->active)return -EBUSY;
    /* Every legacy writer passes this gate. Invalidate conservatively before
     * a possible header write; failed validation may lose cache efficiency only. */
    if(region>=BO_SCENE0 && region<=BO_SCENE1 && offset<BO_HEADER_WORDS*4)
        e->bank_metadata_valid[region-BO_SCENE0]=0;
    return 0;
}
int bo_download_check(struct bo_engine *e,unsigned int region,__u32 offset,__u32 bytes)
{
    if(e->poisoned)return -EIO;
    if(region!=BO_SCRATCH)return -EINVAL;
    return bo_upload_check(e,region,offset,bytes);
}
int bo_copy_check(struct bo_engine *e,const struct bo_mem_copy *c)
{
    __u64 src,dst;int ret;
    if(c->reserved)return -EINVAL;
    ret=bo_upload_check(e,c->dst_region,c->dst_offset,c->bytes);
    if(ret)return ret;
    /* Reading the displayed bank is safe; writing it is never allowed. */
    if(c->src_region>=BO_REGIONS || (c->src_offset&3) ||
       c->src_offset>bo_sizes[c->src_region] ||
       c->bytes>bo_sizes[c->src_region]-c->src_offset)return -EINVAL;
    src=(__u64)bo_offsets[c->src_region]+c->src_offset;
    dst=(__u64)bo_offsets[c->dst_region]+c->dst_offset;
    if(src<dst+c->bytes && dst<src+c->bytes)return -EINVAL;
    return 0;
}
int bo_fill_check(struct bo_engine *e,const struct bo_mem_fill *f)
{
    if(f->reserved || f->value>255)return -EINVAL;
    return bo_upload_check(e,f->region,f->offset,f->bytes);
}
static int projection_result(struct bo_engine *e, struct bo_run *j, unsigned int c)
{
    if(rd(e,c,0x10,&j->status[c])||rd(e,c,0x5c,&j->cells[c]))return -EIO;
    return 0;
}
static int projection_poll(struct bo_engine *e, struct bo_run *j, __u64 end)
{
    unsigned int c,done=0;
    __u32 ctrl;

    for(c=0;c<2;c++)if(!j->jobs[c])done|=1u<<c;
    while(done!=3){
        for(c=0;c<2;c++)if(!(done&(1u<<c))){
            if(rd(e,c,0,&ctrl))return -EIO;
            if(ctrl&2){ /* ap_done is clear-on-read. */
                if(projection_result(e,j,c))return -EIO;
                done|=1u<<c;
            }
        }
        if(done!=3 && expired(e,end))return -ETIMEDOUT;
    }
    return 0;
}
static int projection_wait_irq(struct bo_engine *e, struct bo_run *j, __u64 end)
{
    unsigned int c;
    __u32 ctrl;
    int ret;

    for(c=0;c<2;c++)if(j->jobs[c]){
        ret=e->irq->wait(e->io.opaque,c,end);
        if(ret)return ret;
        if(rd(e,c,0,&ctrl)||!(ctrl&2)||projection_result(e,j,c))return -EIO;
    }
    return 0;
}
static int compare_output_offset(const void *left, const void *right)
{
    __u32 a=*(const __u32 *)left, b=*(const __u32 *)right;

    return (a>b)-(a<b);
}
static int projection_validate(const struct bo_run *j, const __u32 *const desc[2])
{
    __u32 offsets[2*BO_MAX_JOBS];
    unsigned int c,n,count=0;

    for(c=0;c<2;c++)for(n=0;n<j->jobs[c];n++){
        const __u32 *d=desc[c]+n*BO_DESC_WORDS;
        if(d[1]<BO_HEADER_WORDS || d[1]>j->scene_words-BO_TILE_WORDS ||
           !d[2] || d[2]>2048 || !d[3] || d[3]>2048 || d[4]<d[2] ||
           (__u64)d[0]+(__u64)(d[3]-1)*d[4]+d[2]>j->texture_words)return -EINVAL;
        offsets[count++]=d[1];
    }
    /* Tiles have equal length. Sort only the offsets and compare neighbors. */
#ifdef __KERNEL__
    sort(offsets,count,sizeof(offsets[0]),compare_output_offset,NULL);
#else
    qsort(offsets,count,sizeof(offsets[0]),compare_output_offset);
#endif
    for(n=1;n<count;n++)
        if(offsets[n]-offsets[n-1]<BO_TILE_WORDS)return -EINVAL;
    return 0;
}
int bo_run(struct bo_engine *e,struct bo_run *j,const __u32 *d0,const __u32 *d1)
{
    const __u32 *desc[2]={d0,d1}; unsigned int c; __u32 value;__u64 end;
    int ret;
    if(e->poisoned)return -EIO;
    if(j->bank>2 || !scene_ok(j->scene_words) || !timeout_ok(j->timeout_ms) ||
       (j->bank==2 && j->scene_words>BO_SCRATCH_BYTES/4) ||
       !j->texture_words || j->texture_words>bo_sizes[BO_TEXTURE]/4 ||
       j->jobs[0]>BO_MAX_JOBS || j->jobs[1]>BO_MAX_JOBS)return -EINVAL;
    if(j->bank<2 && (int)j->bank==e->active)return -EBUSY;
    memset(j->status,0,sizeof(j->status));memset(j->cells,0,sizeof(j->cells));
    if(!j->jobs[0]&&!j->jobs[1])return 0; /* A clean update does not launch hardware. */
    ret=projection_validate(j,desc);
    if(ret)return ret;
    for(c=0;c<2;c++)if(j->jobs[c]){
        if(rd(e,c,0,&value))return poison(e,-EIO);
        if(!(value&4))return -EBUSY;
    }
    e->io.memory_fence(e->io.opaque);
    for(c=0;c<2;c++){
        if(!j->jobs[c])continue;
        if(wr(e,c,BO_PROJECTION_GIE,0)||wr(e,c,BO_PROJECTION_IER,0)||
           wr(e,c,0x18,e->dma_base+bo_offsets[BO_TEXTURE])||wr(e,c,0x1c,0)||
           wr(e,c,0x24,e->dma_base+bo_offsets[BO_DESC0+c])||wr(e,c,0x28,0)||
           wr(e,c,0x30,e->dma_base+bo_offsets[BO_SCENE0+j->bank])||wr(e,c,0x34,0)||
           wr(e,c,0x3c,j->texture_words)||wr(e,c,0x44,j->jobs[c]*18)||
           wr(e,c,0x4c,j->scene_words)||wr(e,c,0x54,j->jobs[c]))return poison(e,-EIO);
    }
    if(e->irq)
        for(c=0;c<2;c++)if(j->jobs[c])e->irq->arm(e->io.opaque,c);
    for(c=0;c<2;c++)if(j->jobs[c]&&wr(e,c,0,1)){
        ret=-EIO;
        goto finish;
    }
    end=deadline(e,j->timeout_ms);
    ret=e->irq ? projection_wait_irq(e,j,end) : projection_poll(e,j,end);
finish:
    if(e->irq)
        for(c=0;c<2;c++)if(j->jobs[c])e->irq->disarm(e->io.opaque,c);
    if(ret)return poison(e,ret);
    e->io.memory_fence(e->io.opaque);
    return (j->status[0]||j->status[1])?-EIO:0;
}
int bo_stop(struct bo_engine *e,unsigned int ms)
{
    __u32 idle,status;__u64 end;
    if(e->poisoned)return -EIO;
    if(!timeout_ok(ms))return -EINVAL;
    if(wr(e,2,0,0))return poison(e,-EIO);
    /* Let fixed-latency project/normalize stages reach the variable-latency cache. */
    e->io.delay_us(e->io.opaque,10);end=deadline(e,ms);
    for(;;){
        if(rd(e,2,0x40,&idle)||rd(e,2,4,&status))return poison(e,-EIO);
        if((idle&1)&&!(status&0x14)){e->active=-1;e->configured=0;return 0;}
        if(expired(e,end))return poison(e,-ETIMEDOUT);
    }
}
static int present_bounds(struct bo_engine *e,const struct bo_present *p)
{
    if(e->poisoned)return -EIO;
    if(p->bank>1 || !scene_ok(p->scene_words) || p->resolution>2 || !timeout_ok(p->timeout_ms))return -EINVAL;
    if((int)p->bank==e->active)return -EBUSY;
    /* Changing pose/resolution while running needs a separate atomic transaction. */
    if(e->configured && (p->resolution!=e->resolution || memcmp(p->camera,e->camera,sizeof(e->camera))))return -EBUSY;
    return 0;
}
static int present_check(struct bo_engine *e,const struct bo_present *p,const __u32 *scene)
{
    unsigned n;int ret=present_bounds(e,p);if(ret)return ret;
    for(n=256;n<BO_HEADER_WORDS;n++)if(scene[n]!=0xffffffffu &&
        (__u64)scene[n]+(64u<<(2*p->resolution))>(p->scene_words-BO_HEADER_WORDS)*4u)return -EINVAL;
    return 0;
}
static int present_execute(struct bo_engine *e,const struct bo_present *p,const __u32 *scene,int copy_only)
{
    unsigned n;__u32 status,base,command;__u64 end,begin=e->io.now_us(e->io.opaque);
    if(!e->configured){
        if(bo_stop(e,p->timeout_ms))return -EIO;
        if(wr(e,2,0x78,0)||wr(e,2,0x1c,0)||wr(e,2,0x5c,p->resolution)||wr(e,2,0x60,0))return poison(e,-EIO);
        for(n=0;n<180;n++)if(wr(e,2,0x64,p->camera[n]))return poison(e,-EIO);
        if(wr(e,2,0x68,1))return poison(e,-EIO);
    }
    e->profile.configure_us=e->io.now_us(e->io.opaque)-begin;begin=e->io.now_us(e->io.opaque);
    e->io.memory_fence(e->io.opaque);
    base=e->dma_base+bo_offsets[BO_SCENE0+p->bank];
    /* Patch retains BS25's current metadata, so exact equality is required.
     * Only 4476 metadata words are inspected, never inactive cell data. */
    command=!copy_only && e->configured && e->metadata_valid && e->metadata_words==p->scene_words &&
        !memcmp(e->metadata,scene,sizeof(e->metadata)) ? 2 : 1;
    e->profile.select_us=e->io.now_us(e->io.opaque)-begin;e->profile.command=command;begin=e->io.now_us(e->io.opaque);
    if(wr(e,2,8,base)||wr(e,2,0x0c,p->scene_words)||wr(e,2,0x6c,command))return poison(e,-EIO);
    end=deadline(e,p->timeout_ms);
    /* Wait for metadata loaded before first enable; pending is NOT completion. */
    if(!e->configured){
        do{
            if(rd(e,2,4,&status))return poison(e,-EIO);
            if(status&32)return poison(e,-EIO);
            if(expired(e,end))return poison(e,-ETIMEDOUT);
        }while(!(status&16));
        if(wr(e,2,0,1))return poison(e,-EIO);
    }
    for(;;){
        __u32 active;
        if(rd(e,2,4,&status)||rd(e,2,0x44,&active))return poison(e,-EIO);
        if(status&32)return poison(e,-EIO);
        if(active==base && (status&2) && !(status&0x14))break;
        if(expired(e,end))return poison(e,-ETIMEDOUT);
    }
    e->profile.wait_us=e->io.now_us(e->io.opaque)-begin;begin=e->io.now_us(e->io.opaque);
    if(!copy_only)memcpy(e->metadata,scene,sizeof(e->metadata));
    e->metadata_words=p->scene_words;e->metadata_valid=!copy_only;
    e->active=p->bank;e->configured=1;e->resolution=p->resolution;
    memcpy(e->camera,p->camera,sizeof(e->camera));
    e->profile.finish_us=e->io.now_us(e->io.opaque)-begin;return 0;
}

int bo_present(struct bo_engine *e,const struct bo_present *p,const __u32 *scene)
{
    int ret;__u64 begin=e->io.now_us(e->io.opaque);
    memset(&e->profile,0,sizeof(e->profile));ret=present_check(e,p,scene);
    e->profile.validate_us=e->io.now_us(e->io.opaque)-begin;
    return ret?ret:present_execute(e,p,scene,0);
}
int bo_publish(struct bo_engine *e,const struct bo_present *p,const __u32 *metadata,
               int (*upload)(void *,unsigned,__u32,const __u32 *,__u32),void *opaque)
{
    unsigned i,first,bank;int ret,valid;__u64 begin=e->io.now_us(e->io.opaque);
    memset(&e->profile,0,sizeof(e->profile));
    ret=present_check(e,p,metadata);
    e->profile.validate_us=e->io.now_us(e->io.opaque)-begin;
    if(ret)return ret;
    bank=p->bank;valid=e->bank_metadata_valid[bank];
    e->bank_metadata_valid[bank]=0;begin=e->io.now_us(e->io.opaque);
    /* Compare CPU snapshots, never the write-combined scanout mapping. */
    for(i=0;i<BO_HEADER_WORDS;){
        if(valid && e->bank_metadata[bank][i]==metadata[i]){i++;continue;}
        first=i++;
        while(i<BO_HEADER_WORDS && (!valid || e->bank_metadata[bank][i]!=metadata[i]))i++;
        ret=upload(opaque,BO_SCENE0+bank,first,metadata+first,i-first);
        if(ret)return poison(e,ret); /* accepted earlier writes cannot be undone */
        e->profile.uploaded_words+=i-first;e->profile.upload_ranges++;
    }
    memcpy(e->bank_metadata[bank],metadata,sizeof(e->bank_metadata[bank]));
    e->bank_metadata_valid[bank]=1;
    e->profile.upload_us=e->io.now_us(e->io.opaque)-begin;
    /* Snapshot stays alive through acknowledgment. No DDR metadata reread. */
    return present_execute(e,p,metadata,0);
}

int bo_publish_copy(struct bo_engine *e,const struct bo_present *p,const __u32 *metadata,
               int (*upload)(void *,unsigned,__u32,const __u32 *,__u32),void *opaque)
{
    int ret;__u64 begin=e->io.now_us(e->io.opaque);
    memset(&e->profile,0,sizeof(e->profile));ret=present_bounds(e,p);
    e->profile.validate_us=e->io.now_us(e->io.opaque)-begin;
    if(ret)return ret;
    /* A subsequent checked call must not reuse a stale snapshot. */
    e->bank_metadata_valid[p->bank]=0;e->metadata_valid=0;
    begin=e->io.now_us(e->io.opaque);
    ret=upload(opaque,BO_SCENE0+p->bank,0,metadata,BO_HEADER_WORDS);
    if(ret)return poison(e,ret);
    e->profile.uploaded_words=BO_HEADER_WORDS;e->profile.upload_ranges=1;
    e->profile.upload_us=e->io.now_us(e->io.opaque)-begin;
    return present_execute(e,p,metadata,1);
}
