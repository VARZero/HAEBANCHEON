#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "bosio_engine.h"
#define __iomem
#define GFP_KERNEL 0
#define PAGE_SIZE 4096u
#define DMA_TO_DEVICE 1
#define DMA_PREP_INTERRUPT 1
#define DMA_CTRL_ACK 2
#define DMA_TRANS_NOERROR 0
#define DMA_TRANS_ABORTED 1
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define offset_in_page(p) ((unsigned long)(p)&4095)
#define dma_wmb() ((void)0)
#define dma_rmb() ((void)0)
#define msecs_to_jiffies(n) (n)
typedef uintptr_t dma_addr_t;
typedef int dma_cookie_t;
struct completion {int done;};
struct dmaengine_result {int result;unsigned residue;};
struct dma_async_tx_descriptor {
 void (*callback_result)(void *,const struct dmaengine_result *);
 void *callback_param;dma_addr_t dst,src;unsigned bytes;int fill,value;
};
struct dma_chan {int dummy;};
struct miscdevice {int dummy;};
struct mutex {int dummy;};
struct device {int dummy;};
struct page {int dummy;};
struct scatterlist {dma_addr_t address;unsigned length;};
struct sg_table {struct scatterlist *sgl;unsigned orig_nents;};
static unsigned maps,unmaps,frees,issued,completed,submit_count;
static int mode,vmalloc_mode,map_failure,prepare_failure,submit_failure,fail_at;
static struct dma_async_tx_descriptor descriptor;
static struct dma_chan channel;
static unsigned char arena[0x1000000];
static int outstanding;
static int allow_user_dma;
static void *kzalloc(size_t n,int flags){(void)flags;return calloc(1,n);}
static void kfree(void *p){free(p);}
static void kvfree(void *p){frees++;free(p);}
static int sg_alloc_table(struct sg_table *t,unsigned n,int flags)
{(void)flags;t->orig_nents=n;t->sgl=calloc(n,sizeof(*t->sgl));return t->sgl?0:-ENOMEM;}
static void sg_free_table(struct sg_table *t){free(t->sgl);}
static int is_vmalloc_addr(void *p){(void)p;return vmalloc_mode;}
static struct page *virt_to_page(void *p){return (struct page*)((uintptr_t)p&~4095UL);}
static struct page *vmalloc_to_page(void *p){return virt_to_page(p);}
static void sg_set_page(struct scatterlist *s,struct page *p,unsigned n,unsigned off)
{s->address=(uintptr_t)p+off;s->length=n;}
#define for_each_sg(base,sg,count,i) for((i)=0;(i)<(count) && ((sg)=&(base)[i],1);(i)++)
#define sg_dma_address(s) ((s)->address)
#define sg_dma_len(s) ((s)->length)
static struct device *dmaengine_get_dma_device(struct dma_chan *c){(void)c;return NULL;}
static unsigned dma_map_sg(struct device *d,struct scatterlist *s,unsigned n,int dir)
{(void)d;(void)s;(void)dir;if(map_failure)return 0;maps++;return n;}
static void dma_unmap_sg(struct device *d,struct scatterlist *s,unsigned n,int dir)
{(void)d;(void)s;(void)n;(void)dir;assert(!outstanding);unmaps++;}
static void reinit_completion(struct completion *c){c->done=0;}
static void complete(struct completion *c){c->done=1;completed++;}
static struct dma_async_tx_descriptor *dmaengine_prep_dma_memcpy(struct dma_chan *c,dma_addr_t dst,dma_addr_t src,unsigned n,int flags)
{(void)c;(void)flags;if(prepare_failure && (!fail_at || issued+1==(unsigned)fail_at))return NULL;descriptor=(struct dma_async_tx_descriptor){.dst=dst,.src=src,.bytes=n};return &descriptor;}
static struct dma_async_tx_descriptor *dmaengine_prep_dma_memset(struct dma_chan *c,dma_addr_t dst,int value,unsigned n,int flags)
{struct dma_async_tx_descriptor *tx=dmaengine_prep_dma_memcpy(c,dst,0,n,flags);if(tx){tx->fill=1;tx->value=value;}return tx;}
static dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *tx)
{(void)tx;submit_count++;return submit_failure && (!fail_at || submit_count==(unsigned)fail_at)?-EIO:1;}
static int dma_submit_error(dma_cookie_t cookie){return cookie<0;}
static void dma_async_issue_pending(struct dma_chan *c){(void)c;issued++;outstanding=1;}
static void finish_dma(int error)
{
 assert(outstanding);
 assert(allow_user_dma || (descriptor.dst>=0xbd000000 && descriptor.dst+descriptor.bytes<=0xbe000000));
 void *dst=(void *)descriptor.dst;
 if(descriptor.dst>=0xbd000000 && descriptor.dst<0xbe000000)dst=arena+(descriptor.dst-0xbd000000);
 const void *src=(void*)descriptor.src;
 if(descriptor.src>=0xbd000000 && descriptor.src<0xbe000000)src=arena+(descriptor.src-0xbd000000);
 if(!error){if(descriptor.fill)memset(dst,descriptor.value,descriptor.bytes);else memcpy(dst,src,descriptor.bytes);}
 outstanding=0;
 struct dmaengine_result result={.result=error?DMA_TRANS_ABORTED:DMA_TRANS_NOERROR};
 descriptor.callback_result(descriptor.callback_param,&result);
}
static int wait_for_completion_timeout(struct completion *c,unsigned timeout)
{(void)timeout;int fault=!fail_at || issued==(unsigned)fail_at;if(mode==1 && fault)return 0;finish_dma(mode==2 && fault);return c->done;}
