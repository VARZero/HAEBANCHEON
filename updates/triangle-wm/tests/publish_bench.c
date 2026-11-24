/* Compare current publication, explicit metadata copy + present, and new copy ioctl. */
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
#ifndef BO_PUBLISH_COPY
#define BO_PUBLISH_COPY _IOWR('B',0x4f,struct bo_publish)
#endif
static int fd;
static uint32_t metadata[BO_HEADER_WORDS];
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void call(unsigned long op,void*p){if(ioctl(fd,op,p)<0){perror("publish bench");exit(2);}}
int main(int argc,char**argv){
 unsigned maximum=argc>1?2:1;(void)argv;fd=open("/dev/bosio0",O_RDWR);if(fd<0)return 2;
 call(BO_STOP,0);
 for(unsigned i=0;i<BO_HEADER_WORDS;i++)metadata[i]=i<256?initial_palette[i]:UINT32_MAX;
 metadata[256+56]=0;
 struct bo_publish p={0};p.present.scene_words=(BO_HEADER_WORDS+256+15)&~15u;p.present.resolution=2;p.present.timeout_ms=30000;
 memcpy(p.present.camera,initial_camera,sizeof(initial_camera));p.words=BO_HEADER_WORDS;p.metadata=(uintptr_t)metadata;
 for(unsigned bank=0;bank<2;bank++){struct bo_mem_fill f={BO_SCENE0+bank,BO_HEADER_WORDS*4,1024,bank?228:255,0};call(BO_MEM_FILL,&f);}
 p.present.bank=0;metadata[0]=(metadata[0]&0xffffff)|1u<<24;call(BO_PUBLISH,&p);unsigned active=0;
 puts("PUBLISH_BENCH_BEGIN");
 for(unsigned mode=0;mode<=maximum;mode++)for(unsigned changes=0;changes<2;changes++)for(int i=-2;i<20;i++){
  unsigned bank=active^1;p.present.bank=bank;metadata[0]=(metadata[0]&0xffffff)|((bank+1)<<24);
  /* Change metadata on every visit to a bank when requested. */
  metadata[1]=changes?initial_palette[1]^(((unsigned)((i+2)/2))&1?0x00010101u:0):initial_palette[1];
  uint64_t t=ns(),copied=t;memset(&p.profile,0,sizeof(p.profile));
  if(mode==1){struct bo_upload u={BO_SCENE0+bank,0,sizeof(metadata),0,(uintptr_t)metadata};call(BO_UPLOAD,&u);copied=ns();call(BO_PRESENT,&p.present);}
  else call(mode==2?BO_PUBLISH_COPY:BO_PUBLISH,&p);
  uint64_t done=ns();active=bank;
  struct bo_info info={0};call(BO_GET_INFO,&info);if(info.active_bank!=(int)bank||info.poisoned)return 3;
  if(i>=0)printf("PUB %u %u %d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %u %u\n",mode,changes,i,
   (unsigned long long)(done-t),(unsigned long long)(copied-t),(unsigned long long)(done-copied),
   (unsigned long long)p.profile.user_copy_us,(unsigned long long)p.profile.validate_us,(unsigned long long)p.profile.upload_us,
   (unsigned long long)p.profile.configure_us,(unsigned long long)p.profile.select_us,(unsigned long long)p.profile.wait_us,(unsigned long long)p.profile.finish_us,p.profile.uploaded_words,p.profile.command);
 }
 call(BO_STOP,0);close(fd);puts("PUBLISH_BENCH_DONE");return 0;
}
