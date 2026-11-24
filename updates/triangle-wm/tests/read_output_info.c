/* Read-only output status through the WM's existing file description. */
#define _GNU_SOURCE
#include "bosio_uapi.h"
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
int main(int argc,char**argv) {
 int fd;
 if(argc==1)fd=open("/dev/bosio0",O_RDWR);
 else {
 if(argc!=3&&argc!=4)return 2;
 int pid=syscall(434,atoi(argv[1]),0);
 if(pid<0){perror("pidfd_open");return 1;}
 fd=syscall(438,pid,atoi(argv[2]),0);close(pid);
 }
 if(fd<0){perror("pidfd_getfd");return 1;}
 struct bo_info info={0};struct bo_dma_info dma={0};
 if(ioctl(fd,BO_GET_INFO,&info)||ioctl(fd,BO_GET_DMA_INFO,&dma)){perror("info");return 1;}
 printf("OUTPUT_INFO abi=%u signature=%08x capability=%08x status=%08x active_bank=%d active_base=%08x poison=%u dma_features=%x dma_errors=%u dma_timeouts=%u\n",info.abi,info.signature,info.capability,info.status,info.active_bank,info.active_base,info.poisoned,dma.features,dma.errors,dma.timeouts);
 if(argc==4) {
  uint32_t meta[BO_HEADER_WORDS];unsigned char tile[1024];
  struct bo_mem_copy copy={BO_SCENE0+(unsigned)info.active_bank,0,BO_SCRATCH,0,sizeof(meta),0};
  struct bo_upload read={BO_SCRATCH,0,sizeof(meta),0,(uintptr_t)meta};
  if(info.active_bank<0||ioctl(fd,BO_MEM_COPY,&copy)||ioctl(fd,BO_DOWNLOAD,&read)){perror("metadata read");return 1;}
  unsigned visible=0,first=0,source=0;
  for(unsigned i=0;i<4220;i++)if(meta[256+i]!=UINT32_MAX){if(!visible){first=i;source=meta[256+i];}visible++;}
  printf("SCENE visible=%u first_tile=%u source=%u palette84=%08x palette255=%08x\\n",visible,first,source,meta[84],meta[255]);
  if(visible) {
   copy.src_offset=BO_HEADER_WORDS*4+source;copy.bytes=sizeof(tile);read.bytes=sizeof(tile);read.data=(uintptr_t)tile;
   if(ioctl(fd,BO_MEM_COPY,&copy)||ioctl(fd,BO_DOWNLOAD,&read)){perror("tile read");return 1;}
   unsigned nonzero=0,white=0;for(unsigned i=0;i<sizeof(tile);i++){nonzero+=tile[i]!=0;white+=tile[i]==255;}
   printf("FIRST_TILE nonzero_cells=%u white_cells=%u first_index=%u rgb=%06x\\n",nonzero,white,tile[0],meta[tile[0]]&0xffffff);
  }
 }
 close(fd);return 0;
}
