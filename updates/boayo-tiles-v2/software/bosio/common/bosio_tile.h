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
static int bo_tile_apply(struct bo_engine *e,const struct bo_tile_blend *b,
                         const struct bo_tile_io *io)
{
    __u32 dst,src,i=0;int ret;
    if(b->bank>1 || b->tile>=4220 || b->mode>1 || b->color>255 || b->reserved)
        return -EINVAL;
    dst=(BO_HEADER_WORDS+b->tile*BO_TILE_WORDS)*4;
    ret=bo_upload_check(e,BO_SCENE0+b->bank,dst,1024);if(ret)return ret;
    if(!b->mode && (b->scratch_word<BO_HEADER_WORDS ||
        b->scratch_word>BO_SCRATCH_BYTES/4-BO_TILE_WORDS))return -EINVAL;
    if(b->mode && b->scratch_word)return -EINVAL;
    dst+=bo_offsets[BO_SCENE0+b->bank];src=bo_offsets[BO_SCRATCH]+b->scratch_word*4;
    while(i<256){
        __u32 nibble=(b->mask[i/8]>>((i%8)*4))&15;
        if(!nibble){i++;continue;}
        if(nibble==15){
            __u32 first=i++;
            while(i<256 && ((b->mask[i/8]>>((i%8)*4))&15)==15)i++;
            ret=io->transfer(io->opaque,dst+first*4,src+first*4,(i-first)*4,b->mode,b->color);
            if(ret){e->poisoned=1;return ret;}
        }else{
            __u32 mask=0,value,old;unsigned byte;
            for(byte=0;byte<4;byte++)if(nibble&(1u<<byte))mask|=255u<<(byte*8);
            old=io->read(io->opaque,dst+i*4);
            value=b->mode?b->color*0x01010101u:io->read(io->opaque,src+i*4);
            io->write(io->opaque,dst+i*4,(old&~mask)|(value&mask));i++;
        }
    }
    e->io.memory_fence(e->io.opaque);return 0;
}
#endif
