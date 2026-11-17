#ifndef BOSIO_BATCH_H
#define BOSIO_BATCH_H
#include "bosio_tile.h"
/* Also compiled by the host boundary tests: same validation and dispatch. */
static int bo_batch_apply(struct bo_engine *e,const struct bo_batch_entry *entries,
                           unsigned count,const struct bo_tile_io *io)
{
    unsigned i;int ret;
    if(!count || count>BO_BATCH_MAX)return -EINVAL;
    for(i=0;i<count;i++){
        const struct bo_batch_entry *b=entries+i;
        if(b->reserved)return -EINVAL;
        switch(b->kind){
        case BO_BATCH_COPY:ret=bo_copy_check(e,&b->op.copy);break;
        case BO_BATCH_FILL:ret=bo_fill_check(e,&b->op.fill);break;
        case BO_BATCH_BLEND:ret=bo_tile_check(e,&b->op.blend);break;
        default:return -EINVAL;
        }
        if(ret)return ret;
    }
    for(i=0;i<count;i++){
        const struct bo_batch_entry *b=entries+i;
        if(b->kind==BO_BATCH_COPY){const struct bo_mem_copy *c=&b->op.copy;
            ret=io->transfer(io->opaque,bo_offsets[c->dst_region]+c->dst_offset,
                             bo_offsets[c->src_region]+c->src_offset,c->bytes,0,0);
        }else if(b->kind==BO_BATCH_FILL){const struct bo_mem_fill *f=&b->op.fill;
            ret=io->transfer(io->opaque,bo_offsets[f->region]+f->offset,0,f->bytes,1,f->value);
        }else ret=bo_tile_apply(e,&b->op.blend,io);
        if(ret){e->poisoned=1;return ret;}
    }
    e->io.memory_fence(e->io.opaque);return 0;
}
#endif
