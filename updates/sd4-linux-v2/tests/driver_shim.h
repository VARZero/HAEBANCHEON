/* Host model for the unchanged driver request/IRQ functions, not Linux itself. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t dma_addr_t;
typedef int irqreturn_t;
#define __iomem
#define PAGE_SIZE 4096
#define U32_MAX UINT32_MAX
#define BIT(n) (1U<<(n))
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define MMC_RSP_PRESENT 1
#define MMC_RSP_136 2
#define MMC_RSP_CRC 4
#define MMC_RSP_BUSY 8
#define MMC_RSP_OPCODE 16
#define MMC_RSP_R1 (1|4|16)
#define MMC_CMD_AC 0
#define MMC_DATA_WRITE 256
#define MMC_DATA_READ 512
#define MMC_READ_SINGLE_BLOCK 17
#define MMC_READ_MULTIPLE_BLOCK 18
#define MMC_WRITE_BLOCK 24
#define MMC_WRITE_MULTIPLE_BLOCK 25
#define MMC_SEND_STATUS 13
#define R1_READY_FOR_DATA BIT(8)
#define R1_STATUS(x) ((x)&0xFFF9A000U)
#define mmc_resp_type(c) ((c)->flags&31)
#define mmc_ready_for_data(s) (((s)&BIT(8)) && (((s)>>9)&15)==4)
#define mmc_card_is_blockaddr(c) ((c)->blockaddr)
#define lower_32_bits(x) ((u32)(x))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define mmc_dev(h) (h)
#define dev_err(...) ((void)0)
#define dma_wmb() ((void)0)
#define dma_rmb() ((void)0)
struct completion { int done; };
struct work_struct { int unused; };
struct mmc_card { u32 rca; bool blockaddr; };
struct mmc_host { struct mmc_card *card; };
struct scatterlist { unsigned char *buf; size_t len; };
struct mmc_command { u32 opcode,arg,flags,resp[4]; int error; };
struct mmc_data {
 u32 blksz,blocks,flags,bytes_xfered; int error;
 struct scatterlist *sg; unsigned int sg_len;
};
struct mmc_request { struct mmc_command *cmd,*sbc,*stop; struct mmc_data *data; };
static unsigned long jiffies;
#define msecs_to_jiffies(n) (n)
#define time_before(a,b) ((long)((a)-(b))<0)
static void usleep_range(int a,int b) { (void)b;jiffies+=a/1000; }
static void reinit_completion(struct completion *c) { c->done=0; }
static void complete(struct completion *c) { c->done=1; }
static int wait_for_completion_timeout(struct completion *c,unsigned long ticks);
static u32 readl(void *p) { return *(u32*)p; }
static void writel(u32 v,void *p);
static unsigned completed;
static void mmc_request_done(struct mmc_host *h,struct mmc_request *r)
{ (void)h;(void)r;completed++; }
static size_t sg_copy(struct scatterlist *sg,unsigned int n,void *buf,size_t len,
                     size_t off,bool to_buffer)
{
 size_t done=0;
 for(unsigned int i=0;i<n && done<len;i++) {
  if(off>=sg[i].len) {off-=sg[i].len;continue;}
  size_t take=sg[i].len-off;
  if(take>len-done) take=len-done;
  if(to_buffer) memcpy((char*)buf+done,sg[i].buf+off,take);
  else memcpy(sg[i].buf+off,(char*)buf+done,take);
  done+=take;off=0;
 }
 return done;
}
#define sg_pcopy_to_buffer(s,n,b,l,o) sg_copy(s,n,b,l,o,true)
#define sg_pcopy_from_buffer(s,n,b,l,o) sg_copy(s,n,b,l,o,false)
