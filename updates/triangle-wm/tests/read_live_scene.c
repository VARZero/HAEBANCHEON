/* Borrow an existing open description, stage scene reads through scratch.
 * Pause the WM and app first; displayed banks are never written. */
#define _GNU_SOURCE
#include "bosio_uapi.h"
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
static int borrow(int pid,int fd){int p=syscall(434,pid,0);if(p<0){perror("pidfd_open");exit(1);}int r=syscall(438,p,fd,0);close(p);if(r<0){perror("pidfd_getfd");exit(1);}return r;}
static void read_region(int fd,unsigned region,unsigned offset,void*data,unsigned size){
 struct bo_mem_copy c={region,offset,BO_SCRATCH,0,size,0};if(ioctl(fd,BO_MEM_COPY,&c)){perror("stage");exit(1);}
 struct bo_upload u={BO_SCRATCH,0,size,0,(uintptr_t)data};if(ioctl(fd,BO_DOWNLOAD,&u)){perror("download");exit(1);}}
int main(int argc,char**argv){if(argc!=3&&argc!=5)return 2;int fd=borrow(atoi(argv[1]),atoi(argv[2]));struct bo_info info;if(ioctl(fd,BO_GET_INFO,&info)){perror("info");return 1;}
 uint32_t meta[BO_HEADER_WORDS],pixels[240*144];read_region(fd,BO_SCENE0+info.active_bank,0,meta,sizeof(meta));read_region(fd,BO_TEXTURE,0,pixels,sizeof(pixels));
 unsigned white[3]={0};for(unsigned y=0;y<144;y++)for(unsigned x=0;x<240;x++)if((pixels[y*240+x]&0xffffff)==0xffffff)white[y<88?0:y<114?1:2]++;
 printf("TEXTURE white_top=%u white_bar=%u white_counter=%u bank=%d palette255=%08x\n",white[0],white[1],white[2],info.active_bank,meta[255]);
 unsigned total=0,visible=0;unsigned char tile[1024];
 for(unsigned t=0;t<4220;t++)if(meta[256+t]!=UINT32_MAX){read_region(fd,BO_SCENE0+info.active_bank,BO_HEADER_WORDS*4+meta[256+t],tile,sizeof(tile));unsigned n=0;for(unsigned i=0;i<1024;i++)n+=tile[i]==255;if(n){printf("WHITE tile=%u source=%u cells=%u\n",t,meta[256+t],n);total+=n;}visible++;}
 printf("SCENE visible=%u white_cells=%u\n",visible,total);
 if(argc==5){int app=borrow(atoi(argv[3]),atoi(argv[4]));unsigned char source[240*144];if(pread(app,source,sizeof(source),0)!=sizeof(source)){perror("memfd read");return 1;}unsigned n=0;for(unsigned y=0;y<88;y++)for(unsigned x=0;x<240;x++)n+=source[y*240+x]==255;printf("APP_SLOT0 white_top=%u\n",n);close(app);}
 close(fd);return 0;
}
