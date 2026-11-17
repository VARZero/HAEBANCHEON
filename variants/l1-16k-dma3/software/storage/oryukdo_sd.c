// SPDX-License-Identifier: GPL-2.0-only
// Native SD host: page-sized Linux requests, one block per wire command.
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/mmc/card.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>

struct or_sd {
 struct mmc_host *mmc;
 void __iomem *regs;
 void *bounce;
 dma_addr_t dma;
 struct completion completion;
 struct work_struct work;
 struct mmc_request *request;
 u32 status;
 bool dead;
};
static irqreturn_t or_sd_irq(int irq, void *arg)
{
 struct or_sd *s=arg;
 u32 status=readl(s->regs+12);
 if (!(status & 2)) return IRQ_NONE;
 s->status=status;
 writel(2,s->regs+12);
 complete(&s->completion);
 return IRQ_HANDLED;
}
static void or_sd_command(struct or_sd *s, struct mmc_command *cmd,
                          struct mmc_data *data, size_t offset)
{
 u32 c=cmd->opcode, bytes=data ? data->blksz*data->blocks : 0;
 unsigned int type=mmc_resp_type(cmd);
 if (s->dead) { cmd->error=-EIO; return; }
 if (bytes>512 || (bytes & 3) || (data && data->blocks!=1)) {
  cmd->error=-EINVAL; return;
 }
 if (type & MMC_RSP_PRESENT)
  c|=((type & MMC_RSP_136)?2:(type & MMC_RSP_BUSY)?3:1)<<6;
 if (type & MMC_RSP_CRC) c|=BIT(8);
 if (data) {
  c|=BIT(9);
  if (data->flags & MMC_DATA_WRITE) {
   c|=BIT(10);
   if (sg_pcopy_to_buffer(data->sg,data->sg_len,s->bounce,bytes,offset)!=bytes) {
    cmd->error=-EINVAL; return;
   }
  }
 }
 reinit_completion(&s->completion);
 writel(2,s->regs+12);
 writel(lower_32_bits(s->dma),s->regs+24);
 writel(bytes,s->regs+28);
 writel(cmd->arg,s->regs+4);
 dma_wmb();
 writel(c,s->regs+8);
 if (!wait_for_completion_timeout(&s->completion,msecs_to_jiffies(3000))) {
  // An accepted AXI write cannot be cancelled. Quarantine the host and retain
  // the permanently allocated buffer until system reset, never reuse/free it.
  s->dead=true; cmd->error=-ETIMEDOUT;
  if(data) data->error=-ETIMEDOUT;
  dev_err(mmc_dev(s->mmc),"SD timeout: host quarantined until reset\n");
  return;
 }
 if ((s->status>>4)&7) {
  unsigned int code=(s->status>>4)&7;
  cmd->error=code==1?-ETIMEDOUT:code==4?-EINVAL:code==5?-EIO:-EILSEQ;
  if(data) data->error=cmd->error;
  return;
 }
 if (type & MMC_RSP_136) {
  for(int i=0;i<4;i++) cmd->resp[i]=readl(s->regs+44-4*i);
 } else cmd->resp[0]=readl(s->regs+32);
 if(data) {
  dma_rmb();
  if (data->flags & MMC_DATA_READ)
   if(sg_pcopy_from_buffer(data->sg,data->sg_len,s->bounce,bytes,offset)!=bytes) {
    data->error=-EINVAL; return;
   }
  data->bytes_xfered=bytes;
 }
}

/* The PHY waits for DAT0 busy after each write. CMD13 also checks the
 * card's final programming status before counting this block as complete. */
static int or_sd_write_status(struct or_sd *s, u32 *response)
{
 unsigned long deadline=jiffies+msecs_to_jiffies(3000);
 do {
  struct mmc_command status={.opcode=MMC_SEND_STATUS,
   .arg=s->mmc->card->rca<<16,.flags=MMC_RSP_R1|MMC_CMD_AC};
  or_sd_command(s,&status,NULL,0);
  *response=status.resp[0];
  if(status.error) return status.error;
  if(R1_STATUS(status.resp[0]) & ~R1_READY_FOR_DATA) return -EIO;
  if(mmc_ready_for_data(status.resp[0])) return 0;
  usleep_range(1000,2000);
 } while(time_before(jiffies,deadline));
 return -ETIMEDOUT;
}

static void or_sd_blocks(struct or_sd *s, struct mmc_request *mrq)
{
 struct mmc_data *data=mrq->data;
 bool write=mrq->cmd->opcode==MMC_WRITE_BLOCK ||
            mrq->cmd->opcode==MMC_WRITE_MULTIPLE_BLOCK;
 u32 step,block;
 u64 last;
 data->bytes_xfered=0;
 if(!s->mmc->card || data->blksz!=512 || !data->blocks ||
    data->blocks>PAGE_SIZE/512 ||
    (data->flags&(MMC_DATA_READ|MMC_DATA_WRITE))!=
      (write?MMC_DATA_WRITE:MMC_DATA_READ) ||
    (mrq->sbc && mrq->sbc->arg!=data->blocks)) {
  mrq->cmd->error=data->error=-EINVAL;
  return;
 }
 step=mmc_card_is_blockaddr(s->mmc->card)?1:512;
 last=(u64)mrq->cmd->arg+(u64)(data->blocks-1)*step;
 if(last>U32_MAX) {mrq->cmd->error=data->error=-EINVAL;return;}
 /* No CMD23/CMD12 on the wire: every CMD17/CMD24 is self terminating.
  * Advertise a page to Linux, not an unsupported multiblock PHY command. */
 for(block=0;block<data->blocks;block++) {
  struct mmc_command cmd=*mrq->cmd;
  struct mmc_data one=*data;
  cmd.opcode=write?MMC_WRITE_BLOCK:MMC_READ_SINGLE_BLOCK;
  cmd.arg=mrq->cmd->arg+block*step;
  cmd.error=0;
  one.blocks=1;one.bytes_xfered=0;one.error=0;
  or_sd_command(s,&cmd,&one,(size_t)block*512);
  memcpy(mrq->cmd->resp,cmd.resp,sizeof(cmd.resp));
  if(!cmd.error && !one.error && R1_STATUS(cmd.resp[0])) one.error=-EIO;
  if(!cmd.error && !one.error && write) {
   u32 status;
   one.error=or_sd_write_status(s,&status);
   if(mrq->stop) mrq->stop->resp[0]=status;
  } else if(mrq->stop) mrq->stop->resp[0]=cmd.resp[0];
  if(cmd.error || one.error) {
   mrq->cmd->error=cmd.error;
   data->error=one.error?one.error:cmd.error;
   return;
  }
  data->bytes_xfered+=512;
 }
}

static void or_sd_work(struct work_struct *work)
{
 struct or_sd *s=container_of(work,struct or_sd,work);
 struct mmc_request *mrq=s->request;
 if(mrq->data && (mrq->cmd->opcode==MMC_READ_SINGLE_BLOCK ||
    mrq->cmd->opcode==MMC_READ_MULTIPLE_BLOCK ||
    mrq->cmd->opcode==MMC_WRITE_BLOCK ||
    mrq->cmd->opcode==MMC_WRITE_MULTIPLE_BLOCK)) {
  or_sd_blocks(s,mrq);
 } else {
  if(mrq->sbc) or_sd_command(s,mrq->sbc,NULL,0);
  if(!mrq->sbc || !mrq->sbc->error) or_sd_command(s,mrq->cmd,mrq->data,0);
  if(mrq->stop && !s->dead) or_sd_command(s,mrq->stop,NULL,0);
 }
 s->request=NULL;
 mmc_request_done(s->mmc,mrq);
}
static void or_sd_request(struct mmc_host *mmc,struct mmc_request *mrq)
{
 struct or_sd *s=mmc_priv(mmc);
 s->request=mrq;
 schedule_work(&s->work);
}
static void or_sd_set_ios(struct mmc_host *mmc,struct mmc_ios *ios)
{
 struct or_sd *s=mmc_priv(mmc);
 unsigned int hz=ios->clock ? min(ios->clock,25000000U):400000U;
 if(s->dead) return; // Do not reconfigure a quarantined, possibly busy engine.
 writel(DIV_ROUND_UP(100000000U,2*hz),s->regs+16);
 writel(ios->bus_width==MMC_BUS_WIDTH_4,s->regs+20);
}
static const struct mmc_host_ops or_sd_ops={
 .request=or_sd_request,.set_ios=or_sd_set_ios,
};
static int or_sd_probe(struct platform_device *pdev)
{
 struct mmc_host *mmc;
 struct or_sd *s;
 int err,irq;
 mmc=mmc_alloc_host(sizeof(*s),&pdev->dev);
 if(!mmc) return -ENOMEM;
 s=mmc_priv(mmc);s->mmc=mmc;
 s->regs=devm_platform_ioremap_resource(pdev,0);
 if(IS_ERR(s->regs)) {err=PTR_ERR(s->regs);goto free_host;}
 if(readl(s->regs)!=0x53443431) {err=-ENODEV;goto free_host;}
 err=dma_set_mask_and_coherent(&pdev->dev,DMA_BIT_MASK(32));
 if(err) goto free_host;
 s->bounce=dma_alloc_coherent(&pdev->dev,512,&s->dma,GFP_KERNEL);
 if(!s->bounce) {err=-ENOMEM;goto free_host;}
 init_completion(&s->completion);INIT_WORK(&s->work,or_sd_work);
 irq=platform_get_irq(pdev,0);
 if(irq<0) {err=irq;goto free_dma;}
 err=devm_request_irq(&pdev->dev,irq,or_sd_irq,0,dev_name(&pdev->dev),s);
 if(err) goto free_dma;
 mmc->ops=&or_sd_ops;
 mmc->f_min=400000;mmc->f_max=25000000;
 // Longer erase/program commands must use the MMC core's status polling;
 // the native PHY bounds an individual R1b wait to one second.
 mmc->max_busy_timeout=900;
 mmc->ocr_avail=MMC_VDD_32_33|MMC_VDD_33_34;
 mmc->caps=MMC_CAP_4_BIT_DATA|MMC_CAP_NONREMOVABLE;
 mmc->caps2=MMC_CAP2_NO_SDIO|MMC_CAP2_NO_MMC;
 mmc->max_blk_size=512;mmc->max_blk_count=PAGE_SIZE/512;mmc->max_req_size=PAGE_SIZE;
 mmc->max_seg_size=PAGE_SIZE;mmc->max_segs=16;
 platform_set_drvdata(pdev,s);writel(1,s->regs+48);
 err=mmc_add_host(mmc);
 if(!err) return 0;
 writel(0,s->regs+48);
free_dma:
 dma_free_coherent(&pdev->dev,512,s->bounce,s->dma);
free_host:
 mmc_free_host(mmc);return err;
}
static const struct of_device_id or_sd_match[]={
 {.compatible="oryukdo,sd4-v1"},{ }
};
// Built-in only, no hot unplug or power switching exists on this board.
static struct platform_driver or_sd_driver={
 .probe=or_sd_probe,
 .driver={.name="oryukdo-sd4",.of_match_table=or_sd_match,.suppress_bind_attrs=true},
};
builtin_platform_driver(or_sd_driver);
MODULE_LICENSE("GPL");
