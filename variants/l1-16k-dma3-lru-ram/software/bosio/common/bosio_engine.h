#ifndef BOSIO_ENGINE_H
#define BOSIO_ENGINE_H
#include "../include/bosio_uapi.h"
struct bo_io {
    void *opaque;
    int (*read)(void *, unsigned int, __u32, __u32 *);
    int (*write)(void *, unsigned int, __u32, __u32);
    __u64 (*now_us)(void *);
    void (*delay_us)(void *, unsigned int);
    void (*memory_fence)(void *);
};
enum {
    BO_PROJECTION_GIE = 0x04,
    BO_PROJECTION_IER = 0x08,
    BO_PROJECTION_ISR = 0x0c,
    BO_PROJECTION_IRQ_DONE = 1u << 0,
};
struct bo_projection_irq_ops {
    void (*arm)(void *, unsigned int);
    int (*wait)(void *, unsigned int, __u64);
    void (*disarm)(void *, unsigned int);
};
struct bo_engine {
    struct bo_io io;
    const struct bo_projection_irq_ops *irq;
    __u32 dma_base;
    int active, poisoned, configured;
    __u32 resolution, camera[180];
    /* Last successfully committed BS25 metadata; never a cell-data shadow. */
    __u32 metadata[BO_HEADER_WORDS], metadata_words;
    int metadata_valid;
    __u32 bank_metadata[2][BO_HEADER_WORDS];
    int bank_metadata_valid[2];
    struct bo_publish_profile profile;
};
extern const __u32 bo_offsets[BO_REGIONS], bo_sizes[BO_REGIONS];
void bo_init(struct bo_engine *, const struct bo_io *, __u32);
int bo_info(struct bo_engine *, struct bo_info *);
int bo_upload_check(struct bo_engine *, unsigned int, __u32, __u32);
int bo_download_check(struct bo_engine *, unsigned int, __u32, __u32);
int bo_copy_check(struct bo_engine *, const struct bo_mem_copy *);
int bo_fill_check(struct bo_engine *, const struct bo_mem_fill *);
int bo_run(struct bo_engine *, struct bo_run *, const __u32 *, const __u32 *);
int bo_present(struct bo_engine *, const struct bo_present *, const __u32 *);
int bo_publish(struct bo_engine *,const struct bo_present *,const __u32 *,
               int (*upload)(void *,unsigned,__u32,const __u32 *,__u32),void *);
int bo_stop(struct bo_engine *, unsigned int);
int bo_set_pose(struct bo_engine *, const struct bo_pose *);
#endif
