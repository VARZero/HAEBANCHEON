/* Actual board test, no scanout writes. Run before BoAYO takes the device. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include "../software/bosio/include/bosio_uapi.h"
#define N 691200u
static int fill(int fd,void *p,unsigned n,unsigned color)
{
 struct bo_rgb_fill f={.data=(uintptr_t)p,.bytes=n,.color=color};
 int ret=ioctl(fd,BO_RGB_FILL,&f);
 if(ret<0 && errno==ETIMEDOUT)_exit(74);
 return ret;
}
static int check(const unsigned char *p,unsigned n,unsigned color)
{for(unsigned i=0;i<n;i++)if(p[i]!=((color>>(8*(i%3)))&255))return 1;return 0;}
int main(void)
{
 int fd=open("/dev/bosio0",O_RDWR);struct bo_dma_info before={0},after={0};
 unsigned char *p=mmap(NULL,N+8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 if(fd<0 || p==MAP_FAILED)return 1;
 if(ioctl(fd,BO_GET_DMA_INFO,&before)<0 || !(before.features&BO_DMA_RGB_USER))return 2;
 unsigned offsets[]={4,8,4092};unsigned colors[]={0x080503,0x201810,0xfe2107};
 for(unsigned t=0;t<3;t++) {
  unsigned off=offsets[t];p[off-1]=0xa5;p[off+N]=0xa5;
  struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
  if(fill(fd,p+off,N,colors[t]))return 3;
  clock_gettime(CLOCK_MONOTONIC,&b);
  printf("RGB_DMA_DONE offset=%u bytes=%u elapsed_ms=%lld\n",off,N,
    (long long)(b.tv_sec-a.tv_sec)*1000+(b.tv_nsec-a.tv_nsec)/1000000);fflush(stdout);
  if(check(p+off,N,colors[t]) || p[off-1]!=0xa5 || p[off+N]!=0xa5)return 4;
 }
 /* Writable private COW mappings must not alter the parent's page. */
 memset(p,0xa5,4096);pid_t child=fork();if(child<0)return 5;
 if(!child)_exit(fill(fd,p,4096,0x080503) || check(p,4096,0x080503));
 int status;if(waitpid(child,&status,0)<0 || !WIFEXITED(status) || WEXITSTATUS(status))return 6;
 for(unsigned i=0;i<4096;i++)if(p[i]!=0xa5)return 7;
 if(mprotect(p,4096,PROT_READ))return 8;
 errno=0;if(fill(fd,p,4096,0)==0 || errno!=EFAULT)return 9;
 if(ioctl(fd,BO_GET_DMA_INFO,&after)<0 || after.quarantined || after.errors!=before.errors ||
    after.copy_bytes-before.copy_bytes<3*N)return 10;
 puts("PASS RGB_DMA page phases RGB bytes canaries COW readonly rejection completion counters");
 munmap(p,N+8192);close(fd);return 0;
}
