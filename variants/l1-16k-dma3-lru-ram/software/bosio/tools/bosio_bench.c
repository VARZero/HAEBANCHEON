#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../include/bosio_uapi.h"
#include "bosio_bench.h"

enum {
    TILE_BYTES = BO_TILE_WORDS * sizeof(__u32),
    MAX_TILES = 2 * BO_MAX_JOBS,
    MAX_ROUNDS = 1000,
    DEFAULT_ROUNDS = 21,
    SOURCE_EDGE = 32,
    SOURCE_PIXELS = SOURCE_EDGE * SOURCE_EDGE,
    SCRATCH_HEADER = BO_HEADER_WORDS * sizeof(__u32),
    GUARD_BYTE = 0xa5,
};
_Static_assert((2 * MAX_TILES - 1) * TILE_BYTES <= BO_SCRATCH_BYTES, "strided benchmark bounds");
_Static_assert(SCRATCH_HEADER + MAX_TILES * TILE_BYTES <= BO_SCRATCH_BYTES, "projection benchmark bounds");
_Static_assert(MAX_TILES <= BO_BATCH_MAX, "batch benchmark bounds");

static __u32 texture[MAX_TILES * SOURCE_PIXELS];
_Alignas(__u32) static unsigned char payload[MAX_TILES * TILE_BYTES];
static unsigned char expected[BO_SCRATCH_BYTES], readback[BO_SCRATCH_BYTES];
static unsigned char reflected[MAX_TILES * TILE_BYTES];
static uint64_t timings[4][MAX_ROUNDS];

static void request(int fd, unsigned long op, void *arg, const char *name)
{
    if (ioctl(fd, op, arg) < 0) {
        perror(name);
        exit(1);
    }
}

static void transfer(int fd, unsigned long op, unsigned region, unsigned offset,
                     unsigned bytes, const void *data)
{
    struct bo_upload io = {
        .region = region, .offset = offset, .bytes = bytes, .data = (uintptr_t)data,
    };
    request(fd, op, &io, op == BO_DOWNLOAD ? "BO_DOWNLOAD" :
            op == BO_UPLOAD_DIRECT ? "BO_UPLOAD_DIRECT" : "BO_UPLOAD");
}

static uint64_t now_ns(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) {
        perror("CLOCK_MONOTONIC");
        exit(1);
    }
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static int compare_sample(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void report(const char *scenario, const char *phase, unsigned tiles, unsigned bytes,
                   unsigned calls, unsigned rounds, uint64_t *samples)
{
    qsort(samples, rounds, sizeof(*samples), compare_sample);
    uint64_t low_mid = samples[(rounds - 1) / 2], high_mid = samples[rounds / 2];
    printf("%s,%s,%u,%u,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
           scenario, phase, tiles, bytes, calls, rounds, samples[0],
           low_mid + (high_mid - low_mid) / 2, samples[(95 * rounds + 99) / 100 - 1]);
}

static void verify(int fd, const char *scenario, unsigned offset, unsigned bytes,
                   const unsigned char *reference)
{
    transfer(fd, BO_DOWNLOAD, BO_SCRATCH, offset, bytes, readback);
    if (memcmp(readback, reference, bytes)) {
        unsigned pos = 0;
        while (readback[pos] == reference[pos])
            ++pos;
        fprintf(stderr, "%s: scratch byte %u is %02x, expected %02x\n",
                scenario, offset + pos, readback[pos], reference[pos]);
        exit(1);
    }
}

enum transfer_mode { SPAN, PER_TILE, DIRECT, DMA_COPY, DMA_BATCH };
static const struct transfer_case {
    const char *name;
    enum transfer_mode mode;
    unsigned stride_tiles, feature;
} cases[] = {
    {"upload-span", SPAN, 1, 0},
    {"upload-per-tile", PER_TILE, 1, 0},
    {"upload-gapped", PER_TILE, 2, 0},
    {"upload-direct-span", DIRECT, 1, BO_DMA_DIRECT_USER},
    {"dma-copy-span", DMA_COPY, 1, BO_DMA_COPY},
    {"dma-batch-adjacent", DMA_BATCH, 1, BO_DMA_COPY | BO_DMA_BATCH},
    {"dma-batch-gapped", DMA_BATCH, 2, BO_DMA_COPY | BO_DMA_BATCH},
};

static void bench_transfer(int fd, const struct transfer_case *test,
                           unsigned tiles, unsigned rounds)
{
    unsigned bytes = tiles * TILE_BYTES;
    unsigned stride = test->stride_tiles * TILE_BYTES;
    unsigned span = (tiles - 1) * stride + TILE_BYTES;
    struct bo_mem_copy span_copy = {BO_TEXTURE, 0, BO_SCRATCH, 0, bytes, 0};
    struct bo_batch_entry entries[MAX_TILES] = {0};
    struct bo_batch batch = {.count = tiles, .entries = (uintptr_t)entries};
    if (test->mode == DMA_BATCH) {
        for (unsigned tile = 0; tile < tiles; ++tile) {
            entries[tile].kind = BO_BATCH_COPY;
            entries[tile].op.copy = (struct bo_mem_copy){
                BO_TEXTURE, tile * TILE_BYTES, BO_SCRATCH, tile * stride, TILE_BYTES, 0,
            };
        }
    }
    memset(expected, GUARD_BYTE, span);
    transfer(fd, BO_UPLOAD, BO_SCRATCH, 0, span, expected);
    for (unsigned iteration = 0; iteration <= rounds; ++iteration) {
        for (unsigned i = 0; i < bytes; ++i)
            payload[i] = (unsigned char)(i + (i / TILE_BYTES) * 13 + iteration + 1);
        for (unsigned tile = 0; tile < tiles; ++tile)
            memcpy(expected + tile * stride, payload + tile * TILE_BYTES, TILE_BYTES);
        if (test->mode == DMA_COPY || test->mode == DMA_BATCH)
            transfer(fd, BO_UPLOAD, BO_TEXTURE, 0, bytes, payload);

        uint64_t start = now_ns();
        switch (test->mode) {
        case SPAN:
        case DIRECT:
            transfer(fd, test->mode == DIRECT ? BO_UPLOAD_DIRECT : BO_UPLOAD,
                     BO_SCRATCH, 0, bytes, payload);
            break;
        case PER_TILE:
            for (unsigned tile = 0; tile < tiles; ++tile)
                transfer(fd, BO_UPLOAD, BO_SCRATCH, tile * stride, TILE_BYTES,
                         payload + tile * TILE_BYTES);
            break;
        case DMA_COPY:
            request(fd, BO_MEM_COPY, &span_copy, "BO_MEM_COPY");
            break;
        case DMA_BATCH:
            request(fd, BO_BATCH, &batch, "BO_BATCH");
            break;
        }
        uint64_t duration = now_ns() - start;
        verify(fd, test->name, 0, span, expected);
        if (iteration)
            timings[0][iteration - 1] = duration;
    }
    report(test->name, "transfer", tiles, bytes,
           test->mode == PER_TILE ? tiles : 1, rounds, timings[0]);
}

static void projection_pixels(unsigned tiles, unsigned iteration, bool resident)
{
    for (unsigned tile = 0; tile < tiles; ++tile) {
        __u32 *image = texture + tile * SOURCE_PIXELS;
        unsigned char *cells = expected + tile * TILE_BYTES;
        for (unsigned y = 0; y < SOURCE_EDGE; ++y) {
            for (unsigned x = 0; x < SOURCE_EDGE; ++x) {
                unsigned color = (x * 11 + y * 37 + tile * 13 + iteration + 1) & 255;
                image[y * SOURCE_EDGE + x] =
                    ((color & 0xe0u) << 16) | ((color & 0x1cu) << 11) | ((color & 3u) << 6);
            }
            /* Row y has 2*y+1 scene cells. Paired cells sample the same integer x. */
            for (unsigned cell = 0; cell <= 2 * y; ++cell) {
                cells[y * y + cell] =
                    (unsigned char)(cell / 2 * 11 + y * 37 + tile * 13 + iteration + 1);
                if (resident)
                    reflected[tile * TILE_BYTES + y * y + cell] =
                        (unsigned char)((SOURCE_EDGE - 1 - cell / 2) * 11 +
                                        y * 37 + tile * 13 + iteration + 1);
            }
        }
    }
}

static void bench_projection(int fd, unsigned tiles, unsigned rounds, unsigned cores, bool resident)
{
    const char *name = resident ? (cores == 1 ? "resident-core0" : "resident-dual")
                                : (cores == 1 ? "projection-core0" : "projection-dual");
    unsigned texture_bytes = tiles * SOURCE_PIXELS * sizeof(__u32);
    unsigned result_bytes = tiles * TILE_BYTES;
    __u32 descriptors[2][BO_MAX_JOBS][BO_DESC_WORDS] = {0};
    struct bo_run run = {
        .bank = 2, .scene_words = (BO_HEADER_WORDS + tiles * BO_TILE_WORDS + 15) & ~15u,
        .texture_words = tiles * SOURCE_PIXELS, .timeout_ms = 30000,
    };
    for (unsigned tile = 0; tile < tiles; ++tile) {
        unsigned core = tile % cores;
        __u32 *d = descriptors[core][run.jobs[core]++];
        d[0] = tile * SOURCE_PIXELS;
        d[1] = BO_HEADER_WORDS + tile * BO_TILE_WORDS;
        d[2] = d[3] = d[4] = SOURCE_EDGE;
        d[5] = 1;
        d[8] = 1;  /* u = column */
        d[11] = 1; /* v = row */
        d[14] = 1; /* denominator */
    }
    memset(readback, GUARD_BYTE, result_bytes);
    transfer(fd, BO_UPLOAD, BO_SCRATCH, SCRATCH_HEADER, result_bytes, readback);
    for (unsigned iteration = 0; iteration <= rounds; ++iteration) {
        /* Alternate the sampling direction so stale output fails readback
         * without uploading any new texture bytes. */
        bool reverse = resident && (iteration & 1);
        if (resident) {
            for (unsigned core = 0; core < cores; ++core)
                for (unsigned job = 0; job < run.jobs[core]; ++job) {
                    descriptors[core][job][6] = reverse ? SOURCE_EDGE - 1 : 0;
                    descriptors[core][job][8] = reverse ? UINT32_MAX : 1;
                }
        } else {
            projection_pixels(tiles, iteration, false);
        }
        uint64_t start = now_ns();
        uint64_t after_texture = start;
        if (!resident) {
            transfer(fd, BO_UPLOAD, BO_TEXTURE, 0, texture_bytes, texture);
            after_texture = now_ns();
        }
        for (unsigned core = 0; core < cores; ++core)
            transfer(fd, BO_UPLOAD, BO_DESC0 + core, 0,
                     run.jobs[core] * sizeof(descriptors[core][0]), descriptors[core]);
        uint64_t after_descriptors = now_ns();
        request(fd, BO_RUN, &run, "BO_RUN");
        uint64_t after_run = now_ns();
        if (run.status[0] || run.status[1] ||
            run.cells[0] != run.jobs[0] * TILE_BYTES || run.cells[1] != run.jobs[1] * TILE_BYTES) {
            fprintf(stderr, "%s: unexpected status %u,%u or cells %u,%u\n", name,
                    run.status[0], run.status[1], run.cells[0], run.cells[1]);
            exit(1);
        }
        verify(fd, name, SCRATCH_HEADER, result_bytes, reverse ? reflected : expected);
        if (iteration) {
            timings[0][iteration - 1] = after_texture - start;
            timings[1][iteration - 1] = after_descriptors - after_texture;
            timings[2][iteration - 1] = after_run - after_descriptors;
            timings[3][iteration - 1] = after_run - start;
        }
    }
    if (!resident)
        report(name, "texture-upload", tiles, texture_bytes, 1, rounds, timings[0]);
    report(name, "descriptor-upload", tiles, tiles * sizeof(descriptors[0][0]),
           cores, rounds, timings[1]);
    report(name, "run", tiles, result_bytes, 1, rounds, timings[2]);
    report(name, "total", tiles, result_bytes, cores + (resident ? 1 : 2), rounds, timings[3]);
}

static unsigned parse_count(const char *text, unsigned max)
{
    char *end;
    errno = 0;
    unsigned long number = strtoul(text, &end, 10);
    if (*text < '0' || *text > '9' || *end || errno || !number || number > max)
        return 0;
    return (unsigned)number;
}

int bosio_bench(int argc, char **argv, bool resident)
{
    const char *command = resident ? "bench-resident" : "bench";
    if (argc == 1 && !strcmp(argv[0], "--help")) {
        printf("usage: bosioctl %s [tiles [rounds]]\n"
               "tiles: 1-128 (default: 1,4,16,64,128); rounds: 1-1000 (default: 21)\n", command);
        puts(resident ? "CSV measures descriptor upload through projection; texture is uploaded once."
                      : "CSV measures offscreen BOSIO transfer and projection; stop bosio-tiles first.");
        return 0;
    }
    unsigned selected = argc ? parse_count(argv[0], MAX_TILES) : 0;
    unsigned rounds = argc == 2 ? parse_count(argv[1], MAX_ROUNDS) : DEFAULT_ROUNDS;
    if (argc > 2 || (argc && !selected) || !rounds) {
        fprintf(stderr, "usage: bosioctl %s [tiles [rounds]]\n", command);
        return 2;
    }
    int fd = open("/dev/bosio0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("/dev/bosio0 (stop bosio-tiles if it is running)");
        return 1;
    }
    struct bo_info info;
    request(fd, BO_GET_INFO, &info, "BO_GET_INFO");
    if (info.abi != BO_ABI || info.poisoned) {
        fputs("benchmark requires a healthy BOSIO ABI3 device\n", stderr);
        close(fd);
        return 1;
    }
    struct bo_dma_info dma = {0};
    if (ioctl(fd, BO_GET_DMA_INFO, &dma) < 0 && errno != ENOTTY) {
        perror("BO_GET_DMA_INFO");
        close(fd);
        return 1;
    }
    if (dma.quarantined) {
        fputs("BOSIO DMA is quarantined\n", stderr);
        close(fd);
        return 1;
    }
    fprintf(stderr, "BOSIO ABI=%u DMA_features=0x%x staged_upload_DMA_threshold=%u bytes\n",
            info.abi, dma.features, dma.upload_threshold);
    if (resident) {
        unsigned preload_tiles = selected ? selected : MAX_TILES;
        unsigned bytes = preload_tiles * SOURCE_PIXELS * sizeof(__u32);
        projection_pixels(preload_tiles, 0, true);
        transfer(fd, BO_UPLOAD, BO_TEXTURE, 0, bytes, texture);
        fprintf(stderr, "Resident texture preloaded once: %u bytes\n", bytes);
    }
    puts("scenario,phase,tiles,bytes,ioctls,rounds,min_ns,median_ns,p95_ns");
    const unsigned sizes[] = {1, 4, 16, 64, MAX_TILES};
    for (unsigned i = 0; i < (selected ? 1 : sizeof(sizes) / sizeof(sizes[0])); ++i) {
        unsigned tiles = selected ? selected : sizes[i];
        if (resident) {
            bench_projection(fd, tiles, rounds, tiles == 1 ? 1 : 2, true);
            fflush(stdout);
            continue;
        }
        for (unsigned j = 0; j < sizeof(cases) / sizeof(cases[0]); ++j) {
            if ((dma.features & cases[j].feature) == cases[j].feature)
                bench_transfer(fd, &cases[j], tiles, rounds);
        }
        if (tiles <= BO_MAX_JOBS)
            bench_projection(fd, tiles, rounds, 1, false);
        if (tiles > 1)
            bench_projection(fd, tiles, rounds, 2, false);
        fflush(stdout);
    }
    close(fd);
    return 0;
}
