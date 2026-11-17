/* Included after bosio_mem_transfer. All operations hold the device mutex.
 * No userspace address is ever used as a DMA/bus address. */
static void bosio_rgb_release(struct bosio_device *d,struct bo_rgb_mapping *m)
{
    if(m->mapped)dma_unmap_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,
                              m->table.orig_nents,DMA_FROM_DEVICE);
    if(m->table.sgl)sg_free_table(&m->table);
    if(m->pinned)unpin_user_pages_dirty_lock(m->pages,m->pinned,true);
    kvfree(m->pages);kfree(m);
}
static int bosio_rgb_pattern(struct bosio_device *d,__u32 color)
{
    unsigned int i,filled=12;
    int ret;
    if(d->rgb_valid && d->rgb_color==color)return 0;
    d->rgb_valid=false;
    for(i=0;i<12;i++)((u8 *)d->rgb_pattern)[i]=color>>(8*(i%3));
    dma_wmb();
    while(filled<BO_RGB_PATTERN_BYTES) {
        unsigned int n=min(filled,BO_RGB_PATTERN_BYTES-filled);
        ret=bosio_mem_transfer(d,d->rgb_dma+filled,d->rgb_dma,n,0,0);
        if(ret)return ret;
        filled+=n;
    }
    d->rgb_color=color;d->rgb_valid=true;return 0;
}
static int bosio_rgb_user(struct bosio_device *d,const struct bo_rgb_fill *f)
{
    struct bo_rgb_mapping *m;
    struct scatterlist *sg;
    unsigned long address=(unsigned long)f->data;
    unsigned int count,i,done=0;
    long pinned;
    int ret;
    if(d->engine.poisoned || d->dma_wait.quarantined)return -EIO;
    if(f->reserved[0] || f->reserved[1] || f->color>0xffffff ||
       f->data>ULONG_MAX || !f->bytes || f->bytes>BO_RGB_MAX_BYTES ||
       ((address|f->bytes)&3) || address>ULONG_MAX-f->bytes)return -EINVAL;
    if(!access_ok(u64_to_user_ptr(f->data),f->bytes))return -EFAULT;
    count=DIV_ROUND_UP(offset_in_page(address)+f->bytes,PAGE_SIZE);
    m=kzalloc(sizeof(*m),GFP_KERNEL);if(!m)return -ENOMEM;
    m->pages=kvcalloc(count,sizeof(*m->pages),GFP_KERNEL);
    if(!m->pages){kfree(m);return -ENOMEM;}
    /* FOLL_WRITE resolves COW before handing memory to the device. */
    pinned=pin_user_pages_fast(address & PAGE_MASK,count,FOLL_WRITE|FOLL_LONGTERM,m->pages);
    if(pinned>0)m->pinned=pinned;
    if(pinned!=count){ret=pinned<0?pinned:-EFAULT;goto release;}
    ret=sg_alloc_table_from_pages(&m->table,m->pages,count,offset_in_page(address),
                                  f->bytes,GFP_KERNEL);
    if(ret)goto release;
    m->mapped=dma_map_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,
                          m->table.orig_nents,DMA_FROM_DEVICE);
    if(!m->mapped){ret=-EIO;goto release;}
    d->held_rgb=m;
    ret=bosio_rgb_pattern(d,f->color);if(ret)goto failed;
    for_each_sg(m->table.sgl,sg,m->mapped,i) {
        unsigned int offset=0,size=sg_dma_len(sg);
        if((sg_dma_address(sg)|size)&3){ret=-EIO;goto failed;}
        while(offset<size) {
            /* Adding 4 preserves DMA alignment and advances RGB phase by 1. */
            unsigned int phase=4*(done%3);
            unsigned int n=min(size-offset,BO_RGB_PATTERN_BYTES-phase);
            ret=bosio_mem_transfer(d,sg_dma_address(sg)+offset,d->rgb_dma+phase,n,0,0);
            if(ret)goto failed;
            offset+=n;done+=n;
        }
    }
    ret=0;goto release;
failed:
    if(done)d->engine.poisoned=1;
    /* Do not release any destination pages or their DMA mappings on timeout.
     * The application exits without unwinding/reusing its buffer. The device
     * is built in, cannot unbind and remains poisoned until board reset. */
    if(d->dma_wait.quarantined)return -ETIMEDOUT;
release:
    d->held_rgb=NULL;bosio_rgb_release(d,m);return ret;
}
