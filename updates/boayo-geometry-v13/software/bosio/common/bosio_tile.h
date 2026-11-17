#ifndef BOSIO_TILE_H
#define BOSIO_TILE_H
#include "bosio_engine.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif
struct bo_tile_io {
    void *opaque;
    int (*transfer)(void *,__u32,__u32,__u32,int,__u32);
    __u32 (*read)(void *,__u32);
    void (*write)(void *,__u32,__u32);
};
/* Validation completes before any write; offsets are arena-relative bytes. */
static int bo_tile_check(struct bo_engine *e,const struct bo_tile_blend *b)
{
    int ret;
    if(b->bank>1 || b->tile>=4220 || b->mode>1 || b->color>255 || b->reserved)
        return -EINVAL;
    ret=bo_upload_check(e,BO_SCENE0+b->bank,(BO_HEADER_WORDS+b->tile*BO_TILE_WORDS)*4,1024);
    if(ret)return ret;
    if(!b->mode && (b->scratch_word<BO_HEADER_WORDS ||
        b->scratch_word>BO_SCRATCH_BYTES/4-BO_TILE_WORDS))return -EINVAL;
    if(b->mode && b->scratch_word)return -EINVAL;
    return 0;
}
static int bo_tile_execute(struct bo_engine *e,const struct bo_tile_blend *b,
                         const struct bo_tile_io *io)
{
    __u32 dst,src,i=0;int ret;__u64 t=bo_prof_now(e);
    BO_PROF_INC(e,tile_calls,1);
    if(e->fast_paths)e->io.memory_fence(e->io.opaque);
    BO_PROF_ADD(e,tile_fence_us,t);t=bo_prof_now(e);
    dst=(BO_HEADER_WORDS+b->tile*BO_TILE_WORDS)*4+bo_offsets[BO_SCENE0+b->bank];
    src=bo_offsets[BO_SCRATCH]+b->scratch_word*4;
    while(i<256){
        __u32 nibble=(b->mask[i/8]>>((i%8)*4))&15;
        if(!nibble){i++;continue;}
        if(nibble==15){
            __u32 first=i++;
            while(i<256 && ((b->mask[i/8]>>((i%8)*4))&15)==15)i++;
            BO_PROF_ADD(e,tile_mask_us,t);t=bo_prof_now(e);
            if(e->fast_paths && i-first<=16){
                __u32 j;
                for(j=first;j<i;++j)io->write(io->opaque,dst+j*4,
                    b->mode?b->color*0x01010101u:io->read(io->opaque,src+j*4));
                BO_PROF_INC(e,tile_cpu_words,i-first);BO_PROF_ADD(e,tile_cpu_us,t);
            }else{
                if(e->fast_paths)e->io.memory_fence(e->io.opaque);
                ret=io->transfer(io->opaque,dst+first*4,src+first*4,(i-first)*4,b->mode,b->color);
                BO_PROF_ADD(e,tile_dma_us,t);BO_PROF_INC(e,tile_dma_calls,1);BO_PROF_INC(e,tile_dma_bytes,(i-first)*4);
                if(ret){e->poisoned=1;return ret;}
            }
            t=bo_prof_now(e);
        }else{
            __u32 mask=0,value,old;unsigned byte;
            for(byte=0;byte<4;byte++)if(nibble&(1u<<byte))mask|=255u<<(byte*8);
            BO_PROF_ADD(e,tile_mask_us,t);t=bo_prof_now(e);
            old=io->read(io->opaque,dst+i*4);
            value=b->mode?b->color*0x01010101u:io->read(io->opaque,src+i*4);
            io->write(io->opaque,dst+i*4,(old&~mask)|(value&mask));i++;
            BO_PROF_INC(e,tile_cpu_words,1);BO_PROF_ADD(e,tile_cpu_us,t);t=bo_prof_now(e);
        }
    }
    BO_PROF_ADD(e,tile_mask_us,t);t=bo_prof_now(e);
    e->io.memory_fence(e->io.opaque);BO_PROF_ADD(e,tile_fence_us,t);return 0;
}
static int bo_tile_apply(struct bo_engine *e,const struct bo_tile_blend *b,const struct bo_tile_io *io)
{
    int ret=bo_tile_check(e,b);return ret?ret:bo_tile_execute(e,b,io);
}
#endif
