/* Device mutex held. Read-only user/file pages are DMA sources, never targets. */
static void bosio_direct_release(struct bosio_device *d,struct bo_rgb_mapping *m)
{
    if(m->mapped)dma_unmap_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,
                              m->table.orig_nents,DMA_TO_DEVICE);
    if(m->table.sgl)sg_free_table(&m->table);
    if(m->pinned)unpin_user_pages(m->pages,m->pinned);
    kvfree(m->pages);kfree(m);
}
static int bosio_direct_user(struct bosio_device *d,const struct bo_upload *u)
{
    struct bo_rgb_mapping *m;
    struct scatterlist *sg;
    unsigned long address=(unsigned long)u->data;
    unsigned int count,i,done=0;
    long pinned;
    int ret;
    if(d->engine.poisoned || d->dma_wait.quarantined)return -EIO;
    if(u->reserved || u->data>ULONG_MAX || (address&3) || address>ULONG_MAX-u->bytes)return -EINVAL;
    ret=bo_upload_check(&d->engine,u->region,u->offset,u->bytes);if(ret)return ret;
    if(!access_ok(u64_to_user_ptr(u->data),u->bytes))return -EFAULT;
    count=DIV_ROUND_UP(offset_in_page(address)+u->bytes,PAGE_SIZE);
    m=kzalloc(sizeof(*m),GFP_KERNEL);if(!m)return -ENOMEM;
    m->pages=kvcalloc(count,sizeof(*m->pages),GFP_KERNEL);
    if(!m->pages){kfree(m);return -ENOMEM;}
    // No FOLL_WRITE: mmap(PROT_READ) assets remain read-only, no COW copy.
    pinned=pin_user_pages_fast(address & PAGE_MASK,count,FOLL_LONGTERM,m->pages);
    if(pinned>0)m->pinned=pinned;
    if(pinned!=count){ret=pinned<0?pinned:-EFAULT;goto release;}
    ret=sg_alloc_table_from_pages(&m->table,m->pages,count,offset_in_page(address),u->bytes,GFP_KERNEL);
    if(ret)goto release;
    m->mapped=dma_map_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,m->table.orig_nents,DMA_TO_DEVICE);
    if(!m->mapped){ret=-EIO;goto release;}
    d->held_direct=m;
    for_each_sg(m->table.sgl,sg,m->mapped,i){
        unsigned int n=sg_dma_len(sg);
        if((sg_dma_address(sg)|n)&3 || n>u->bytes-done){ret=-EIO;goto failed;}
        ret=bosio_mem_transfer(d,d->dma+bo_offsets[u->region]+u->offset+done,sg_dma_address(sg),n,0,0);
        if(ret)goto failed;
        done+=n;
    }
    if(done!=u->bytes){ret=-EIO;goto failed;}
    d->dma_info.upload_ops++;d->dma_info.upload_bytes+=done;
    dma_wmb();ret=0;goto release;
failed:
    if(done)d->engine.poisoned=1;
    // An accepted late DMA read must not see unpinned/reused source memory.
    if(d->dma_wait.quarantined)return -ETIMEDOUT;
release:
    d->held_direct=NULL;bosio_direct_release(d,m);return ret;
}
