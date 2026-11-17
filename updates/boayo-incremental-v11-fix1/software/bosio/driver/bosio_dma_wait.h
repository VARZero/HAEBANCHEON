/* The owning device and callback state live until reset, including timeout.
 * Callers serialize operations and retain every source/destination on timeout. */
struct bo_dma_wait {
    struct completion done;
    struct dmaengine_result result;
    bool quarantined;
    bool submitted;
};
static void bo_dma_completed(void *opaque,const struct dmaengine_result *result)
{
    struct bo_dma_wait *w=opaque;
    if(result)w->result=*result;
    else w->result.result=DMA_TRANS_ABORTED;
    complete(&w->done);
}
static int bo_dma_wait_for(struct bo_dma_wait *w,struct dma_chan *channel,
                          struct dma_async_tx_descriptor *tx)
{
    dma_cookie_t cookie;
    if(w->quarantined)return -EIO;
    w->submitted=false;
    if(!tx)return -ENOMEM;
    reinit_completion(&w->done);
    w->result.result=DMA_TRANS_ABORTED;
    tx->callback_result=bo_dma_completed;tx->callback_param=w;
    dma_wmb();cookie=dmaengine_submit(tx);
    if(dma_submit_error(cookie))return cookie;
    w->submitted=true;
    dma_async_issue_pending(channel);
    if(!wait_for_completion_timeout(&w->done,msecs_to_jiffies(30000))) {
        /* Never treat submission/acceptance as completion or cancel AXI beats. */
        w->quarantined=true;
        return -ETIMEDOUT;
    }
    dma_rmb();
    return w->result.result==DMA_TRANS_NOERROR?0:-EIO;
}
