#ifndef BOSIO_UAPI_H
#define BOSIO_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define BO_ABI 3
#define BO_ARENA_BYTES 0x1000000u
#define BO_MAX_WORDS 1084816u
#define BO_MAX_JOBS 64u
#define BO_DESC_WORDS 18u
#define BO_HEADER_WORDS 4476u
#define BO_TILE_WORDS 256u
/* Scratch is disjoint from both scanout banks and never presented. */
#define BO_SCRATCH_BYTES 0x40000u
enum { BO_TEXTURE, BO_DESC0, BO_DESC1, BO_SCENE0, BO_SCENE1, BO_SCRATCH, BO_REGIONS };
struct bo_upload { __u32 region, offset, bytes, reserved; __aligned_u64 data; };
struct bo_run {
    __u32 bank, scene_words, texture_words, jobs[2], timeout_ms;
    __u32 status[2], cells[2];
};
struct bo_present {
    __u32 bank, scene_words, resolution, timeout_ms;
    __u32 camera[180];
};
struct bo_info {
    __u32 abi, signature, capability, status, active_base;
    __s32 active_bank;
    __u32 poisoned, max_jobs, max_scene_words;
};
struct bo_pose { __u32 timeout_ms; __u32 camera[180]; };
/* Optional ABI3 extension; old structures/ioctl numbers stay unchanged. */
#define BO_DMA_COPY 1u
#define BO_DMA_FILL 2u
#define BO_DMA_UPLOAD 4u
#define BO_DMA_RGB_USER 8u
#define BO_DMA_DIRECT_USER 16u
#define BO_DMA_TILE_BLEND 32u
/* One changed tile only. mode 0 copies selected scratch bytes; mode 1 paints
 * selected bytes with color. mask bit i selects RGB332 byte i. */
struct bo_tile_blend {
    __u32 bank, tile, scratch_word, mode, color, reserved;
    __u32 mask[32];
};
#define BO_TILE_BLEND _IOW('B', 0x4c, struct bo_tile_blend)
#define BO_RGB_MAX_BYTES (3u * 1024u * 1024u)
/* Synchronous RGB24 fill of writable, 4-byte-aligned user memory.
 * The caller must not access/reuse/unmap the range until return. On ETIMEDOUT
 * it must terminate: pages remain pinned until reset for a possible late DMA.
 * color is R | G<<8 | B<<16, independent of host byte order. */
struct bo_rgb_fill { __aligned_u64 data; __u32 bytes, color, reserved[2]; };
struct bo_mem_copy {
    __u32 src_region, src_offset, dst_region, dst_offset, bytes, reserved;
};
/* value is one byte, repeated throughout a 4-byte-aligned range. */
struct bo_mem_fill { __u32 region, offset, bytes, value, reserved; };
struct bo_dma_info {
    __u32 version, features, upload_threshold, quarantined;
    __aligned_u64 copy_ops, copy_bytes, fill_ops, fill_bytes, upload_ops, upload_bytes;
    __u32 errors, timeouts;
};
#define BO_GET_INFO _IOR('B', 0x40, struct bo_info)
#define BO_UPLOAD _IOW('B', 0x41, struct bo_upload)
#define BO_RUN _IOWR('B', 0x42, struct bo_run)
#define BO_PRESENT _IOW('B', 0x43, struct bo_present)
#define BO_STOP _IO('B', 0x44)
#define BO_DOWNLOAD _IOW('B', 0x45, struct bo_upload)
#define BO_SET_POSE _IOW('B', 0x46, struct bo_pose)
#define BO_MEM_COPY _IOW('B', 0x47, struct bo_mem_copy)
#define BO_MEM_FILL _IOW('B', 0x48, struct bo_mem_fill)
#define BO_GET_DMA_INFO _IOR('B', 0x49, struct bo_dma_info)
#define BO_RGB_FILL _IOW('B', 0x4a, struct bo_rgb_fill)
/* Synchronous source-only pinned-page DMA; no temporary pixel buffer.
 * Source must remain immutable/mapped until return. ETIMEDOUT poisons the
 * device and retains mappings/pins until board reset. Never pass bus addresses. */
#define BO_UPLOAD_DIRECT _IOW('B', 0x4b, struct bo_upload)
/* Ordered synchronous commands. Entire list is validated before any write.
 * On execution failure earlier commands may have completed; device is poisoned.
 * Entries are copied once, never reread from mutable userspace during execution. */
#define BO_DMA_BATCH 64u
#define BO_BATCH_MAX 128u
#define BO_BATCH_COPY 0u
#define BO_BATCH_FILL 1u
#define BO_BATCH_BLEND 2u
struct bo_batch_entry {
    __u32 kind, reserved;
    union { struct bo_mem_copy copy; struct bo_mem_fill fill; struct bo_tile_blend blend; } op;
};
struct bo_batch { __u32 count, reserved; __aligned_u64 entries; };
#define BO_BATCH _IOW('B', 0x4d, struct bo_batch)
/* Full immutable CPU metadata snapshot; driver writes only changed ranges and
 * commits under the same mutex. No caller-supplied dirty mask or bus address. */
#define BO_DMA_PUBLISH 128u
struct bo_publish_profile {
    __aligned_u64 validate_us, upload_us, configure_us, select_us, wait_us, finish_us, user_copy_us;
    __u32 uploaded_words, upload_ranges, command, reserved;
};
struct bo_publish {
    struct bo_present present;
    __aligned_u64 metadata;
    __u32 words, reserved;
    struct bo_publish_profile profile;
};
#define BO_PUBLISH _IOWR('B', 0x4e, struct bo_publish)
#endif
