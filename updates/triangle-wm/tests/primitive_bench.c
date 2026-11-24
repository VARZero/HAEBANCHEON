/* Single-thread, direct ABI3 measurements. No WM/JSON/input/projector path. */
#include "bosio_uapi.h"
#include "primitive_init.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int fd;
static int publish_copy;
static uint32_t metadata[BO_HEADER_WORDS],source[256],readback[256];
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void call(unsigned long op,void*p){if(ioctl(fd,op,p)<0){fprintf(stderr,"ioctl=%lx ",op);perror("BOSIO");exit(2);}}
static void mask_for(struct bo_tile_blend*b,unsigned side,int scatter){
 unsigned n=side*side;memset(b->mask,0,sizeof(b->mask));
 if(scatter){for(unsigned i=0;i<n;i++){unsigned cell=(i*257u)%1024;b->mask[cell/32]|=1u<<(cell%32);}}
 else for(unsigned row=0;row<side;row++)for(unsigned k=0;k<2*row+1;k++){unsigned cell=row*row+k;b->mask[cell/32]|=1u<<(cell%32);}
}
static struct bo_publish publish(unsigned bank){
 struct bo_publish p={0};p.present.bank=bank;p.present.scene_words=(BO_HEADER_WORDS+256+15)&~15u;p.present.resolution=2;p.present.timeout_ms=30000;
 metadata[0]=(metadata[0]&0xffffffu)|((bank+1)<<24);
 memcpy(p.present.camera,initial_camera,sizeof(initial_camera));p.words=BO_HEADER_WORDS;p.metadata=(uintptr_t)metadata;call(publish_copy?BO_PUBLISH_COPY:BO_PUBLISH,&p);return p;
}
int main(int argc,char**argv){
 publish_copy=argc>1;(void)argv;
 fd=open("/dev/bosio0",O_RDWR);if(fd<0){perror("open");return 2;}
 struct bo_info info={0};call(BO_GET_INFO,&info);if(info.abi!=3||info.poisoned)return 2;call(BO_STOP,0);
 for(unsigned i=0;i<BO_HEADER_WORDS;i++)metadata[i]=i<256?initial_palette[i]:UINT32_MAX;
 metadata[256+56]=0; /* One visible spherical triangle references payload zero. */
 for(unsigned i=0;i<256;i++)source[i]=0xe4e4e4e4u;
 struct bo_upload upload={BO_SCRATCH,BO_HEADER_WORDS*4,1024,0,(uintptr_t)source};call(BO_UPLOAD,&upload);
 for(unsigned bank=0;bank<2;bank++){struct bo_mem_fill fill={BO_SCENE0+bank,BO_HEADER_WORDS*4,1024,84,0};call(BO_MEM_FILL,&fill);}
 publish(0);unsigned active=0;
 puts("PRIMITIVE_BEGIN");fflush(stdout);
 /* mode: metadata-only, DMA fill/copy/upload full tile, masked solid/copy. */
 for(unsigned mode=0;mode<6;mode++)for(unsigned layout=0;layout<(mode>=4?2u:1u);layout++)for(unsigned side=1;side<=32;side*=2){
  if(mode<4&&side!=32)continue;
  struct bo_tile_blend blend={0};blend.mode=mode==4;blend.scratch_word=blend.mode?0:BO_HEADER_WORDS;mask_for(&blend,side,layout);
  /* Two bank warmups, then ten samples; keep printf outside timed intervals. */
  for(int sample=-2;sample<10;sample++){
   unsigned bank=active^1,color=(sample&1)?228:255;blend.bank=bank;blend.color=mode==4?color:0;
   struct bo_mem_fill fill={BO_SCENE0+bank,BO_HEADER_WORDS*4,1024,84,0};call(BO_MEM_FILL,&fill);
   memset(source,color,1024);upload.region=BO_SCRATCH;call(BO_UPLOAD,&upload);
   uint64_t begin=ns(),staged=begin;
   if(mode==1){fill.value=color;call(BO_MEM_FILL,&fill);}
   if(mode==2){struct bo_mem_copy copy={BO_SCRATCH,BO_HEADER_WORDS*4,BO_SCENE0+bank,BO_HEADER_WORDS*4,1024,0};call(BO_MEM_COPY,&copy);}
   if(mode==3){upload.region=BO_SCENE0+bank;call(BO_UPLOAD,&upload);}
   if(mode==4)call(BO_TILE_BLEND,&blend);
   if(mode==5){upload.region=BO_SCRATCH;call(BO_UPLOAD,&upload);staged=ns();call(BO_TILE_BLEND,&blend);}
   uint64_t written=ns();struct bo_publish p=publish(bank);uint64_t finished=ns();active=bank;
   if(sample==9){
   struct bo_mem_copy verify={BO_SCENE0+bank,BO_HEADER_WORDS*4,BO_SCRATCH,(BO_HEADER_WORDS+256)*4,1024,0};call(BO_MEM_COPY,&verify);
   struct bo_upload dl={BO_SCRATCH,(BO_HEADER_WORDS+256)*4,1024,0,(uintptr_t)readback};call(BO_DOWNLOAD,&dl);
   for(unsigned i=0;i<1024;i++){
    unsigned expected=(mode==0)?84:(mode<4||((blend.mask[i/32]>>(i%32))&1))?color:84;
    if(((unsigned char*)readback)[i]!=expected){fprintf(stderr,"PIXEL_MISMATCH mode=%u cell=%u\n",mode,i);return 3;}
   }}
   if(sample>=0)printf("P %u %u %u %d %llu %llu %llu %llu %llu %llu %llu %u\n",mode,layout,side,sample,(unsigned long long)(written-begin),(unsigned long long)(written-staged),(unsigned long long)(finished-written),(unsigned long long)(finished-begin),(unsigned long long)p.profile.validate_us,(unsigned long long)p.profile.upload_us,(unsigned long long)p.profile.wait_us,p.profile.uploaded_words);
  }
 }
 call(BO_STOP,0);close(fd);puts("PRIMITIVE_DONE");return 0;
}
