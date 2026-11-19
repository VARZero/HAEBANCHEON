#define _POSIX_C_SOURCE 200809L
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
#include "bosio_uapi.h"
#include "bosio_publish_metadata.h"
#include "bosio_tile_publish_geometry.h"
#define ROUNDS 21
#define SCENE_WORDS ((BO_HEADER_WORDS+BO_TILE_WORDS+15u)&~15u)
static uint32_t metadata[BO_HEADER_WORDS];
static uint32_t transport_metadata[BO_HEADER_WORDS];
static int device=-1,active=-1;
static unsigned current_tile=4220;
static int bank_tag;
struct sample {uint64_t total_ns,ioctl_ns;struct bo_publish_profile p;};
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(2);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void call(unsigned long command,void *arg){if(ioctl(device,command,arg)<0){perror("BOSIO ioctl");if(device>=0)close(device);exit(1);}}
static struct sample publish(unsigned tile){
 uint64_t start=now();struct bo_publish p={0};
 if(current_tile<4220)metadata[256+current_tile]=UINT32_MAX;
 if(tile<4220)metadata[256+tile]=0;
 current_tile=tile;
 p.present.bank=active<0?0:(unsigned)active^1u;p.present.scene_words=SCENE_WORDS;
 // BS25 stores palette words in 24-bit RAM. The upper byte is invisible to
 // scanout, but gives the existing kernel comparison an early difference.
 p.present.resolution=2;p.present.timeout_ms=30000;
 memcpy(p.present.camera,tile_camera,sizeof(tile_camera));p.words=BO_HEADER_WORDS;
 if(bank_tag){
  memcpy(transport_metadata,metadata,sizeof(metadata));
  transport_metadata[0]=bosio_bs25_palette_bank_tag(transport_metadata[0],p.present.bank);
  p.metadata=(uintptr_t)transport_metadata;
 }else p.metadata=(uintptr_t)metadata;
 uint64_t io=now();call(BO_PUBLISH,&p);uint64_t end=now();active=p.present.bank;
 struct bo_info info;call(BO_GET_INFO,&info);
 if(info.active_bank!=active || info.poisoned){fputs("BOSIO_VERIFY_FAILED\n",stderr);exit(3);}
 return (struct sample){end-start,end-io,p.profile};
}
static int compare(const void *a,const void *b){uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return (x>y)-(x<y);}
static void report(const char *name,struct sample *samples){
 uint64_t values[ROUNDS];
 for(unsigned i=0;i<ROUNDS;i++){
  struct sample *s=&samples[i];values[i]=s->total_ns;
  printf("BOSIO_TILE_RAW %s,%u,%"PRIu64",%"PRIu64",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u\n",name,i+1,s->total_ns,s->ioctl_ns,
   (unsigned long long)s->p.user_copy_us,(unsigned long long)s->p.validate_us,(unsigned long long)s->p.upload_us,
   (unsigned long long)s->p.configure_us,(unsigned long long)s->p.select_us,(unsigned long long)s->p.wait_us,
   (unsigned long long)s->p.finish_us,s->p.uploaded_words,s->p.upload_ranges,s->p.command);
 }
 qsort(values,ROUNDS,sizeof(values[0]),compare);
 printf("BOSIO_TILE_SUMMARY %s rounds=%u min_ns=%"PRIu64" median_ns=%"PRIu64" p95_ns=%"PRIu64"\n",name,ROUNDS,values[0],values[ROUNDS/2],values[(95*ROUNDS+99)/100-1]);
}
int main(int argc,char **argv){
 if(argc==2 && !strcmp(argv[1],"--bank-tag"))bank_tag=1;
 else if(argc!=1){fprintf(stderr,"usage: %s [--bank-tag]\n",argv[0]);return 2;}
 device=open("/dev/bosio0",O_RDWR|O_CLOEXEC);if(device<0){perror("/dev/bosio0");return 1;}
 struct bo_info info;call(BO_GET_INFO,&info);if(info.abi!=BO_ABI || info.signature!=0x42533235u || info.poisoned){fputs("BOSIO_NOT_READY\n",stderr);close(device);return 1;}
 uint64_t setup=now();
 for(unsigned i=0;i<256;i++)metadata[i]=(((i>>5)*255/7)<<16)|((((i>>2)&7)*255/7)<<8)|((i&3)*255/3);
 for(unsigned i=256;i<BO_HEADER_WORDS;i++)metadata[i]=UINT32_MAX;
 // Identical white cell data is installed once in both inactive scene banks.
 // Activation and movement below change only directory metadata.
 for(unsigned bank=0;bank<2;bank++){struct bo_mem_fill f={BO_SCENE0+bank,BO_HEADER_WORDS*4,BO_TILE_WORDS*4,255,0};call(BO_MEM_FILL,&f);}
 struct sample blank=publish(4220);uint64_t setup_ns=now()-setup;
 struct sample enables[ROUNDS],moves[ROUNDS];
 printf("BOSIO_TILE_SETUP scene_words=%u payload_bytes=%u initialized_banks=2 setup_ns=%"PRIu64" blank_publish_ns=%"PRIu64" bank_tag=%d\n",SCENE_WORDS,BO_TILE_WORDS*4,setup_ns,blank.total_ns,bank_tag);
 for(unsigned n=0;n<=ROUNDS;n++){
  if(n)publish(4220);
  struct sample s=publish(tile_path[0]);if(n)enables[n-1]=s;
 }
 for(unsigned n=0;n<=ROUNDS;n++){struct sample s=publish(tile_path[1+n%(TILE_PATH_COUNT-1)]);if(n)moves[n-1]=s;}
 report("activate-one-tile",enables);report("move-one-tile",moves);
 call(BO_GET_INFO,&info);printf("BOSIO_TILE_VERIFIED active=%d poisoned=%u path=",info.active_bank,info.poisoned);
 for(unsigned i=0;i<TILE_PATH_COUNT;i++)printf("%s%u",i?",":"",tile_path[i]);putchar('\n');
 struct sample cleanup=publish(4220);call(BO_STOP,NULL);close(device);device=-1;
 printf("BOSIO_TILE_CLEANUP blank_ns=%"PRIu64"\nBOSIO_TILE_MOVE_OK\n",cleanup.total_ns);
 return 0;
}
