/* Built-in driver client: scratch only; never changes display metadata. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../software/bosio/include/bosio_uapi.h"
static uint32_t source[1024],result[1024];
int main(void)
{
 int fd=open("/dev/bosio0",O_RDWR);struct bo_dma_info before={0},after={0};
 struct bo_upload u={.region=BO_SCRATCH,.bytes=sizeof(source),.data=(uintptr_t)source};
 struct bo_mem_copy c={BO_SCRATCH,0,BO_SCRATCH,4096,4096,0};
 struct bo_mem_fill f={BO_SCRATCH,8192,4096,0x5a,0};
 if(fd<0){perror("open bosio");return 1;}
 if(ioctl(fd,BO_GET_DMA_INFO,&before)<0 || before.version!=1 || before.quarantined)goto fail;
 for(unsigned i=0;i<1024;i++)source[i]=(i*0x9e3779b9u)^0x51424f53u;
 if(ioctl(fd,BO_UPLOAD,&u)<0 || ioctl(fd,BO_MEM_COPY,&c)<0)goto fail;
 u.offset=4096;u.data=(uintptr_t)result;
 if(ioctl(fd,BO_DOWNLOAD,&u)<0 || memcmp(source,result,sizeof(source)))goto fail;
 if(ioctl(fd,BO_MEM_FILL,&f)<0)goto fail;
 u.offset=8192;
 if(ioctl(fd,BO_DOWNLOAD,&u)<0)goto fail;
 for(unsigned i=0;i<1024;i++)if(result[i]!=0x5a5a5a5a)goto fail;
 c.dst_offset=4;errno=0;
 if(ioctl(fd,BO_MEM_COPY,&c)!=-1 || errno!=EINVAL)goto fail;
 if(ioctl(fd,BO_GET_DMA_INFO,&after)<0 || after.quarantined || after.errors!=before.errors ||
    after.copy_bytes-before.copy_bytes<8192 || after.fill_bytes-before.fill_bytes<4096 ||
    after.upload_bytes-before.upload_bytes!=4096)goto fail;
 printf("PASS BOSIO_DMA upload=4096 copy=4096 fill=4096 overlap_rejected irq_completion\n");
 printf("copy_ops=%llu fill_ops=%llu upload_ops=%llu\n",(unsigned long long)after.copy_ops,
        (unsigned long long)after.fill_ops,(unsigned long long)after.upload_ops);
 close(fd);return 0;
fail:
 fprintf(stderr,"FAIL BOSIO_DMA errno=%d\n",errno);close(fd);return 1;
}
