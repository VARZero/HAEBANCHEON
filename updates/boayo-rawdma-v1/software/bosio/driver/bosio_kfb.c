// Built-in Linux 6.12 driver. DMA buffers live for the platform's whole lifetime.
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "../common/bosio_engine.h"
#include "bosio_dma_wait.h"
#define BO_UPLOAD_DMA_THRESHOLD 4096u
struct bo_upload_mapping {
    void *buffer;
    struct sg_table table;
    unsigned int mapped;
};
struct bo_rgb_mapping {
    struct page **pages;
    unsigned int pinned, mapped;
    struct sg_table table;
};
#define BO_RGB_PATTERN_BYTES 12288u
#ifdef MODULE
#error "BOSIO must be built in: outstanding DMA must survive close and timeout"
#endif
struct bosio_device {
    struct miscdevice misc;
    struct mutex mutex;
    struct bo_engine engine;
    void __iomem *regs[3];
    void *arena;
    dma_addr_t dma;
    bool opened;
    struct dma_chan *channel;
    struct bo_dma_wait dma_wait;
    struct bo_dma_info dma_info;
    struct bo_upload_mapping *held_upload;
    struct bo_rgb_mapping *held_rgb;
    struct bo_rgb_mapping *held_direct;
    void *rgb_pattern;
    dma_addr_t rgb_dma;
    __u32 rgb_color;
    bool rgb_valid;
};
static int bosio_mem_transfer(struct bosio_device *d,dma_addr_t dst,
                             dma_addr_t src,unsigned int bytes,int fill,int value)
{
    struct dma_async_tx_descriptor *tx;
    int ret;
    if(d->engine.poisoned || d->dma_wait.quarantined)return -EIO;
    if(fill)tx=dmaengine_prep_dma_memset(d->channel,dst,value,bytes,DMA_PREP_INTERRUPT|DMA_CTRL_ACK);
    else tx=dmaengine_prep_dma_memcpy(d->channel,dst,src,bytes,DMA_PREP_INTERRUPT|DMA_CTRL_ACK);
    ret=bo_dma_wait_for(&d->dma_wait,d->channel,tx);
    if(ret) {
        d->dma_info.errors++;
        if(ret==-ETIMEDOUT)d->dma_info.timeouts++;
        if(d->dma_wait.submitted)d->engine.poisoned=1;
        return ret;
    }
    if(fill){d->dma_info.fill_ops++;d->dma_info.fill_bytes+=bytes;}
    else {d->dma_info.copy_ops++;d->dma_info.copy_bytes+=bytes;}
    return 0;
}
static void bosio_unmap_upload(struct bosio_device *d,struct bo_upload_mapping *m)
{
    if(m->mapped)dma_unmap_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,
                            m->table.orig_nents,DMA_TO_DEVICE);
    sg_free_table(&m->table);kvfree(m->buffer);kfree(m);
}
static int bosio_upload_dma(struct bosio_device *d,void *buffer,dma_addr_t dst,unsigned int bytes)
{
    struct bo_upload_mapping *m;
    struct scatterlist *sg;
    unsigned int pages=DIV_ROUND_UP(offset_in_page(buffer)+bytes,PAGE_SIZE);
    unsigned int i,left=bytes,done=0;
    unsigned long addr=(unsigned long)buffer;
    int ret;
    m=kzalloc(sizeof(*m),GFP_KERNEL);
    if(!m){kvfree(buffer);return -ENOMEM;}
    m->buffer=buffer;
    ret=sg_alloc_table(&m->table,pages,GFP_KERNEL);
    if(ret){kvfree(buffer);kfree(m);return ret;}
    for_each_sg(m->table.sgl,sg,pages,i) {
        unsigned int off=offset_in_page(addr),size=min(left,(unsigned int)PAGE_SIZE-off);
        struct page *page=is_vmalloc_addr((void *)addr)?vmalloc_to_page((void *)addr):virt_to_page((void *)addr);
        if(!page){ret=-EFAULT;goto release;}
        sg_set_page(sg,page,size,off);addr+=size;left-=size;
    }
    m->mapped=dma_map_sg(dmaengine_get_dma_device(d->channel),m->table.sgl,
                         m->table.orig_nents,DMA_TO_DEVICE);
    if(!m->mapped){ret=-EIO;goto release;}
    d->held_upload=m;
    for_each_sg(m->table.sgl,sg,m->mapped,i) {
        unsigned int size=sg_dma_len(sg);
        ret=bosio_mem_transfer(d,dst+done,sg_dma_address(sg),size,0,0);
        if(ret) {
            if(done)d->engine.poisoned=1;
            /* A late DMA read must never access a freed/mapped-again page. */
            if(d->dma_wait.quarantined)return ret;
            goto release;
        }
        done+=size;
    }
    d->dma_info.upload_ops++;d->dma_info.upload_bytes+=done;
    ret=0;
release:
    d->held_upload=NULL;bosio_unmap_upload(d,m);return ret;
}
#include "bosio_rgb_dma.h"
#include "bosio_direct_dma.h"
static int hw_read(void *opaque,unsigned int core,__u32 reg,__u32 *value)
{ struct bosio_device *d=opaque;*value=readl(d->regs[core]+reg);return 0; }
static int hw_write(void *opaque,unsigned int core,__u32 reg,__u32 value)
{ struct bosio_device *d=opaque;writel(value,d->regs[core]+reg);return 0; }
static __u64 hw_now(void *opaque) { return ktime_to_us(ktime_get()); }
static void hw_delay(void *opaque,unsigned int us) { usleep_range(us,us+50); }
static void hw_barrier(void *opaque) { dma_wmb();dma_rmb(); }
static int bosio_open(struct inode *inode,struct file *file)
{
    struct bosio_device *d=container_of(file->private_data,struct bosio_device,misc);
    int ret=0;
    if(!capable(CAP_SYS_RAWIO))return -EPERM;
    mutex_lock(&d->mutex);
    if(d->opened)ret=-EBUSY;
    else if(d->engine.poisoned)ret=-EIO;
    else {d->opened=true;file->private_data=d;}
    mutex_unlock(&d->mutex);return ret;
}
static int bosio_release(struct inode *inode,struct file *file)
{
    struct bosio_device *d=file->private_data;
    mutex_lock(&d->mutex);
    /* Never free on close. A timeout cannot cancel an accepted AXI transfer. */
    if(!d->engine.poisoned)bo_stop(&d->engine,1000);
    d->opened=false;
    mutex_unlock(&d->mutex);return 0;
}
static long bosio_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct bosio_device *d=file->private_data;
    void __user *u=(void __user *)arg;
    long ret=0;
    if(mutex_lock_interruptible(&d->mutex))return -ERESTARTSYS;
    switch(cmd){
    case BO_GET_INFO: {
        struct bo_info info;
        ret=bo_info(&d->engine,&info);
        if(!ret&&copy_to_user(u,&info,sizeof(info)))ret=-EFAULT;
        break;
    }
    case BO_UPLOAD_DIRECT: {
        struct bo_upload upload;
        if(copy_from_user(&upload,u,sizeof(upload))){ret=-EFAULT;break;}
        ret=bosio_direct_user(d,&upload);break;
    }
    case BO_UPLOAD: {
        struct bo_upload upload;void *tmp;
        if(copy_from_user(&upload,u,sizeof(upload))){ret=-EFAULT;break;}
        if(upload.reserved || upload.data>ULONG_MAX){ret=-EINVAL;break;}
        ret=bo_upload_check(&d->engine,upload.region,upload.offset,upload.bytes);
        if(ret)break;
        tmp=vmemdup_user(u64_to_user_ptr(upload.data),upload.bytes);
        if(IS_ERR(tmp)){ret=PTR_ERR(tmp);break;}
        if(upload.bytes>=BO_UPLOAD_DMA_THRESHOLD)
            ret=bosio_upload_dma(d,tmp,d->dma+bo_offsets[upload.region]+upload.offset,upload.bytes);
        else {memcpy(d->arena+bo_offsets[upload.region]+upload.offset,tmp,upload.bytes);kvfree(tmp);dma_wmb();}
        break;
    }
    case BO_DOWNLOAD: {
        struct bo_upload download;
        if(copy_from_user(&download,u,sizeof(download))){ret=-EFAULT;break;}
        if(download.reserved || download.data>ULONG_MAX){ret=-EINVAL;break;}
        ret=bo_download_check(&d->engine,download.region,download.offset,download.bytes);
        if(ret)break;
        /* BO_RUN returns only after both masters complete. The mutex also
         * excludes uploads and new jobs while the coherent result is copied. */
        dma_rmb();
        if(copy_to_user(u64_to_user_ptr(download.data),
                        d->arena+bo_offsets[download.region]+download.offset,download.bytes))ret=-EFAULT;
        break;
    }
    case BO_RUN: {
        struct bo_run run;
        if(copy_from_user(&run,u,sizeof(run))){ret=-EFAULT;break;}
        ret=bo_run(&d->engine,&run,d->arena+bo_offsets[BO_DESC0],d->arena+bo_offsets[BO_DESC1]);
        if(copy_to_user(u,&run,sizeof(run)))ret=-EFAULT;
        break;
    }
    case BO_PRESENT: {
        struct bo_present *present=memdup_user(u,sizeof(*present));
        if(IS_ERR(present)){ret=PTR_ERR(present);break;}
        if(present->bank>1)ret=-EINVAL;
        else ret=bo_present(&d->engine,present,d->arena+bo_offsets[BO_SCENE0+present->bank]);
        kfree(present);break;
    }
    case BO_SET_POSE: {
        struct bo_pose *pose=memdup_user(u,sizeof(*pose));
        if(IS_ERR(pose)){ret=PTR_ERR(pose);break;}
        ret=bo_set_pose(&d->engine,pose);kfree(pose);break;
    }
    case BO_STOP: ret=bo_stop(&d->engine,1000);break;
    case BO_MEM_COPY: {
        struct bo_mem_copy c;
        if(copy_from_user(&c,u,sizeof(c))){ret=-EFAULT;break;}
        ret=bo_copy_check(&d->engine,&c);if(ret)break;
        ret=bosio_mem_transfer(d,d->dma+bo_offsets[c.dst_region]+c.dst_offset,
                              d->dma+bo_offsets[c.src_region]+c.src_offset,c.bytes,0,0);
        break;
    }
    case BO_MEM_FILL: {
        struct bo_mem_fill f;
        if(copy_from_user(&f,u,sizeof(f))){ret=-EFAULT;break;}
        ret=bo_fill_check(&d->engine,&f);if(ret)break;
        ret=bosio_mem_transfer(d,d->dma+bo_offsets[f.region]+f.offset,0,f.bytes,1,f.value);
        break;
    }
    case BO_GET_DMA_INFO:
        d->dma_info.quarantined=d->dma_wait.quarantined;
        if(copy_to_user(u,&d->dma_info,sizeof(d->dma_info)))ret=-EFAULT;
        break;
    case BO_RGB_FILL: {
        struct bo_rgb_fill f;
        if(copy_from_user(&f,u,sizeof(f))){ret=-EFAULT;break;}
        ret=bosio_rgb_user(d,&f);break;
    }
    default: ret=-ENOTTY;
    }
    mutex_unlock(&d->mutex);return ret;
}
static const struct file_operations bosio_fops={
    .owner=THIS_MODULE,.open=bosio_open,.release=bosio_release,
    .unlocked_ioctl=bosio_ioctl,
};
static int bosio_probe(struct platform_device *pdev)
{
    struct device *dev=&pdev->dev;
    struct bosio_device *d;
    struct device_node *node;
    struct reserved_mem *rmem;
    struct bo_info info;
    struct bo_io io;
    int c,ret;
    d=devm_kzalloc(dev,sizeof(*d),GFP_KERNEL);if(!d)return -ENOMEM;
    for(c=0;c<3;c++){
        struct resource *r=platform_get_resource(pdev,IORESOURCE_MEM,c);
        if(!r || resource_size(r)!=0x80 || r->start!=0x20000000+c*0x10000)return -EINVAL;
        d->regs[c]=devm_ioremap_resource(dev,r);if(IS_ERR(d->regs[c]))return PTR_ERR(d->regs[c]);
    }
    node=of_parse_phandle(dev->of_node,"memory-region",0);
    if(!node)return -EINVAL;
    rmem=of_reserved_mem_lookup(node);
    ret=(!rmem || rmem->base!=0xbd000000 || rmem->size!=BO_ARENA_BYTES ||
         !of_property_read_bool(node,"no-map"))?-EINVAL:0;
    of_node_put(node);if(ret)return ret;
    ret=dma_set_mask_and_coherent(dev,DMA_BIT_MASK(32));if(ret)return ret;
    d->channel=dma_request_chan(dev,"memory");
    if(IS_ERR(d->channel))return dev_err_probe(dev,PTR_ERR(d->channel),"BOSIO memory DMA unavailable\n");
    if(!dma_has_cap(DMA_MEMCPY,d->channel->device->cap_mask) ||
       !dma_has_cap(DMA_MEMSET,d->channel->device->cap_mask)){ret=-EINVAL;goto release_channel;}
    init_completion(&d->dma_wait.done);
    d->dma_info.version=1;d->dma_info.features=BO_DMA_COPY|BO_DMA_FILL|BO_DMA_UPLOAD|BO_DMA_RGB_USER|BO_DMA_DIRECT_USER;
    d->dma_info.upload_threshold=BO_UPLOAD_DMA_THRESHOLD;
    ret=of_reserved_mem_device_init(dev);if(ret)goto release_channel;
    d->arena=dma_alloc_coherent(dev,BO_ARENA_BYTES,&d->dma,GFP_KERNEL);
    if(!d->arena){ret=-ENOMEM;goto release_pool;}
    if(d->dma!=0xbd000000){ret=-EINVAL;goto free_dma;}
    io=(struct bo_io){d,hw_read,hw_write,hw_now,hw_delay,hw_barrier};
    bo_init(&d->engine,&io,(__u32)d->dma);
    ret=bo_info(&d->engine,&info);
    if(ret || info.signature!=0x42533235 || info.capability!=0x44544631 || (info.status&0x15)){
        ret=-ENODEV;goto free_dma;
    }
    for(c=0;c<2;c++)if(!(readl(d->regs[c])&4)){ret=-EBUSY;goto free_dma;}
    /* dma_alloc_coherent already zeroed the entire arena. Explicit later
     * initialization uses BO_MEM_FILL; never clear displayed memory here. */
    mutex_init(&d->mutex);
    d->misc=(struct miscdevice){.minor=MISC_DYNAMIC_MINOR,.name="bosio0",.fops=&bosio_fops,.parent=dev,.mode=0600};
    d->rgb_pattern=dma_alloc_coherent(dmaengine_get_dma_device(d->channel),
                        BO_RGB_PATTERN_BYTES,&d->rgb_dma,GFP_KERNEL);
    if(!d->rgb_pattern){ret=-ENOMEM;goto free_dma;}
    ret=misc_register(&d->misc);if(ret)goto free_rgb;
    platform_set_drvdata(pdev,d);
    dev_info(dev,"BOSIO ABI3: dual projectors + BS25, DMA copy/fill/upload/RGB-user, 16MiB arena\n");
    return 0;
free_rgb:
    dma_free_coherent(dmaengine_get_dma_device(d->channel),BO_RGB_PATTERN_BYTES,
                       d->rgb_pattern,d->rgb_dma);
free_dma:
    dma_free_coherent(dev,BO_ARENA_BYTES,d->arena,d->dma);
release_pool:
    of_reserved_mem_device_release(dev);
release_channel:
    dma_release_channel(d->channel);return ret;
}
static const struct of_device_id bosio_of_match[]={{.compatible="oryukdo,kfb-bosio-v1"},{}};
static struct platform_driver bosio_driver={
    .probe=bosio_probe,
    .driver={.name="bosio-kfb",.of_match_table=bosio_of_match,.suppress_bind_attrs=true},
};
builtin_platform_driver(bosio_driver);
MODULE_DESCRIPTION("ORYUKDO KFB BOSIO controlled DMA and dual projection");
MODULE_LICENSE("GPL");
