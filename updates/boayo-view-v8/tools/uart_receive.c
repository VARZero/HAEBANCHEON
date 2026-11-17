#include <asm/unistd.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>
static long sc(long n,long a,long b,long c,long d){register long a0 __asm__("a0")=a;register long a1 __asm__("a1")=b;register long a2 __asm__("a2")=c;register long a3 __asm__("a3")=d;register long a7 __asm__("a7")=n;__asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a7):"memory");return a0;}
void *memcpy(void*d,const void*s,unsigned n){volatile char*q=d;const volatile char*p=s;while(n--)*q++=*p++;return d;}
void *memset(void*d,int v,unsigned n){volatile char*q=d;while(n--)*q++=v;return d;}
static int wr(int fd,const void *p,unsigned n){while(n){long k=sc(__NR_write,fd,(long)p,n,0);if(k<=0)return -1;p=(char*)p+k;n-=k;}return 0;}
static int rd(void*p,unsigned n){int idle=0;while(n){long k=sc(__NR_read,0,(long)p,n,0);if(k<0)return -1;if(!k){if(++idle==15)return -1;continue;}idle=0;p=(char*)p+k;n-=k;}return 0;}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int rdhex(void*p,unsigned n){char encoded[2048];if(n>1024 || rd(encoded,n*2))return -1;for(unsigned i=0;i<n;i++){int a=nibble(encoded[i*2]),b=nibble(encoded[i*2+1]);if(a<0 || b<0)return -1;((unsigned char*)p)[i]=(a<<4)|b;}return 0;}
static void hex(char*p,unsigned v){for(int i=0;i<8;i++)p[i]="0123456789abcdef"[(v>>(28-4*i))&15];}
int entry(unsigned *stack){int fd=-1,result=1;struct termios old,raw;char buf[256];unsigned h[4],offset=0,hash=2166136261u;
 if(stack[0]!=2)return 2;
 if(sc(__NR_ioctl,0,TCGETS,(long)&old,0)<0)return 3;
 raw=old;raw.c_iflag=0;raw.c_oflag=0;raw.c_lflag=0;raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=20;
 if(sc(__NR_ioctl,0,TCSETS,(long)&raw,0)<0)return 4;
 wr(1,"RX_READY\n",9);
 if(rdhex(h,sizeof(h)) || h[0]!=0x31585242u || h[1]==0 || h[1]>8u*1024*1024 || h[3]!=~h[1])goto done;
 fd=sc(__NR_openat,-100,stack[2],0101|01000,0755);if(fd<0)goto done;
 while(offset<h[1]){unsigned n=h[1]-offset;if(n>sizeof(buf))n=sizeof(buf);if(rdhex(buf,n))goto done;
  for(unsigned i=0;i<n;i++){hash^=(unsigned char)buf[i];hash*=16777619u;}
  if(wr(fd,buf,n))goto done;offset+=n;char ack[]="RX_ACK 00000000 00000000\n";hex(ack+7,offset);hex(ack+16,hash);if(wr(1,ack,25))goto done;
 }
 if(hash!=h[2])goto done;result=0;
 done:if(fd>=0)sc(__NR_close,fd,0,0,0);sc(__NR_ioctl,0,TCSETS,(long)&old,0);wr(1,result?"RX_FAIL\n":"RX_DONE\n",8);return result;
}
__asm__(".global _start\n_start:\nmv a0,sp\ncall entry\nli a7,93\necall\n");
