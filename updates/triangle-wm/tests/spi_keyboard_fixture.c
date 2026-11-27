/* Test-only spidev ioctl fixture, never included in the SD image. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
int ioctl(int fd,unsigned long request,...)
{
 va_list ap;void *arg;struct stat a,b;const char *path=getenv("KEYBOARD_SPI_FIXTURE");va_start(ap,request);arg=va_arg(ap,void *);va_end(ap);
 if(path&&!fstat(fd,&a)&&!stat(path,&b)&&a.st_ino==b.st_ino&&a.st_dev==b.st_dev){
  if(request==SPI_IOC_WR_MODE)return *(uint8_t *)arg==SPI_MODE_1?0:-1;
  if(request==SPI_IOC_WR_BITS_PER_WORD||request==SPI_IOC_WR_MAX_SPEED_HZ)return 0;
  if(request==SPI_IOC_MESSAGE(1)){
   struct spi_ioc_transfer *t=arg;if(t->len!=20)return -1;
   if(pread(fd,(void *)(uintptr_t)t->rx_buf,20,0)!=20)return -1;
   if(pwrite(fd,(void *)(uintptr_t)t->tx_buf,20,20)!=20)return -1;
   return 20;
  }
 }
 int (*real_ioctl)(int,unsigned long,...)=dlsym(RTLD_NEXT,"ioctl");return real_ioctl(fd,request,arg);
}
