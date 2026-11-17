#include <limits.h>
typedef uint8_t u8;
#define PAGE_MASK (~4095UL)
#define DMA_FROM_DEVICE 2
#define FOLL_WRITE 1
#define FOLL_LONGTERM 2
static unsigned pins,unpins,dirty_pins;
static int pin_failure,access_failure;
static int merged_pages;
static unsigned expected_pin_flags=FOLL_WRITE|FOLL_LONGTERM;
static void *kvcalloc(size_t n,size_t size,int flags){(void)flags;return calloc(n,size);}
static void *u64_to_user_ptr(uint64_t a){return (void *)(uintptr_t)a;}
static int access_ok(void *p,unsigned n){(void)p;(void)n;return !access_failure;}
static long pin_user_pages_fast(unsigned long a,unsigned n,unsigned flags,struct page **pages)
{
 assert(flags==expected_pin_flags);unsigned count=pin_failure?n/2:n;
 for(unsigned i=0;i<count;i++)pages[i]=(struct page *)(a+i*4096);
 pins+=count;return count;
}
static void unpin_user_pages_dirty_lock(struct page **pages,unsigned n,bool dirty)
{(void)pages;assert(!outstanding);unpins+=n;if(dirty)dirty_pins+=n;}
static int sg_alloc_table_from_pages(struct sg_table *t,struct page **pages,unsigned n,
                                    unsigned off,unsigned long bytes,int flags)
{
 if(merged_pages) {
  int ret=sg_alloc_table(t,1,flags);if(ret)return ret;
  sg_set_page(t->sgl,pages[0],bytes,off);return 0;
 }
 int ret=sg_alloc_table(t,n,flags);if(ret)return ret;
 for(unsigned i=0;i<n;i++) {
  unsigned len=min(bytes,4096-off);sg_set_page(&t->sgl[i],pages[i],len,off);
  bytes-=len;off=0;
 }
 assert(!bytes);return 0;
}

static void unpin_user_pages(struct page **pages,unsigned n){unpin_user_pages_dirty_lock(pages,n,false);}
