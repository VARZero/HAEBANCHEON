#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../common/bosio_demo.h"
#include "bosio_bench.h"
static void call(int fd,unsigned long op,void *arg)
{if(ioctl(fd,op,arg)<0){perror("bosio ioctl");exit(1);}}

static void upload(int fd, unsigned region, unsigned offset,
                   const void *data, unsigned bytes)
{
    struct bo_upload u = {
        .region = region, .offset = offset, .bytes = bytes, .data = (uintptr_t)data,
    };
    call(fd, BO_UPLOAD, &u);
}

static void project(int fd, unsigned bank, unsigned cores)
{
    const __u32 texture[] = {0xff0000, 0x00ff00};
    struct bo_run run = {
        .bank = bank, .scene_words = BO_DEMO_WORDS, .texture_words = 2,
        .jobs = {cores & 1, (cores >> 1) & 1}, .timeout_ms = 30000,
    };

    upload(fd, BO_TEXTURE, 0, texture, sizeof(texture));
    for (unsigned core = 0; core < 2; core++) {
        __u32 desc[BO_DESC_WORDS];
        if (!run.jobs[core])
            continue;
        bo_demo_descriptor(desc, core, 3);
        upload(fd, BO_DESC0 + core, 0, desc, sizeof(desc));
    }
    int rc = ioctl(fd, BO_RUN, &run);
    int error = errno;
    printf("PROJECT status=%u,%u cells=%u,%u\n",
           run.status[0], run.status[1], run.cells[0], run.cells[1]);
    if (rc < 0) {
        errno = error;
        perror("BO_RUN");
        exit(1);
    }
    for (unsigned core = 0; core < 2; core++) {
        if (run.cells[core] != run.jobs[core] * BO_TILE_WORDS * 4) {
            fprintf(stderr, "projection%u: unexpected cell count %u\n", core, run.cells[core]);
            exit(1);
        }
    }
}

static void project_check(int fd, unsigned cores)
{
    const unsigned offset = BO_HEADER_WORDS * sizeof(__u32);
    __u32 tiles[2][BO_TILE_WORDS];
    struct bo_upload readback = {
        .region = BO_SCRATCH, .offset = offset, .bytes = sizeof(tiles),
        .data = (uintptr_t)tiles,
    };

    /* Seed both tiles so stale data cannot pass an execution check. */
    memset(tiles, 0xcc, sizeof(tiles));
    upload(fd, BO_SCRATCH, offset, tiles, sizeof(tiles));
    project(fd, 2, cores); /* BO_RUN bank 2 is scratch. */
    call(fd, BO_DOWNLOAD, &readback);
    for (unsigned core = 0; core < 2; core++) {
        const __u32 projected[] = {0xe0e0e0e0u, 0x1c1c1c1cu};
        __u32 expected = (cores & (1u << core)) ? projected[core] : 0xccccccccu;
        for (unsigned word = 0; word < BO_TILE_WORDS; word++) {
            if (tiles[core][word] != expected) {
                fprintf(stderr, "projection%u: scratch word %u = %08x, expected %08x\n",
                        core, word, tiles[core][word], expected);
                exit(1);
            }
        }
    }
    puts("PROJECT_OK scratch readback verified");
}

static void usage(FILE *stream)
{
    fprintf(stream, "usage: bosioctl info | project [0|1|both] | demo | "
                    "bench [tiles [rounds]] | bench-resident [tiles [rounds]]\n");
}

int main(int argc,char **argv)
{
    int fd;struct bo_info info;
    unsigned cores = 3;
    if (argc >= 2 && (!strcmp(argv[1], "bench") || !strcmp(argv[1], "bench-resident")))
        return bosio_bench(argc - 2, argv + 2, !strcmp(argv[1], "bench-resident"));
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }
    if (argc < 2 || argc > 3) {
        usage(stderr);
        return 2;
    }
    if (!strcmp(argv[1], "project")) {
        if (argc == 3 && !strcmp(argv[2], "0"))
            cores = 1;
        else if (argc == 3 && !strcmp(argv[2], "1"))
            cores = 2;
        else if (argc == 3 && strcmp(argv[2], "both")) {
            usage(stderr);
            return 2;
        }
    } else if (argc != 2 || (strcmp(argv[1], "info") && strcmp(argv[1], "demo"))) {
        usage(stderr);
        return 2;
    }
    fd=open("/dev/bosio0",O_RDWR|O_CLOEXEC);if(fd<0){perror("/dev/bosio0");return 1;}
    call(fd,BO_GET_INFO,&info);
    printf("BOSIO ABI=%u signature=%08x active=%d poison=%u\n",info.abi,info.signature,info.active_bank,info.poisoned);
    if (!strcmp(argv[1], "project")) {
        project_check(fd, cores);
    } else if(!strcmp(argv[1],"demo")){
        __u32 scene[BO_DEMO_WORDS];
        struct bo_present present={.bank=0,.scene_words=BO_DEMO_WORDS,.resolution=2,.timeout_ms=30000};
        bo_demo_scene(scene);
        bo_demo_camera(present.camera);
        upload(fd, BO_SCENE0, 0, scene, sizeof(scene));
        project(fd, 0, 3);
        call(fd,BO_PRESENT,&present);
        puts("DISPLAY_ACTIVE: left red / right green; press Enter to stop");getchar();
        call(fd,BO_STOP,NULL);
    }
    close(fd);return 0;
}
