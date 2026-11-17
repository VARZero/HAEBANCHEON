#include <asm/unistd.h>
typedef unsigned U;
#ifdef HOST
#include <unistd.h>
static long sc(long n,long a,long b,long c,long d,long e,long f){return syscall(n,a,b,c,d,e,f);}
#define MAPCALL __NR_mmap
#else
static long sc(long n,long a,long b,long c,long d,long e,long f){register long a0 __asm__("a0")=a;register long a1 __asm__("a1")=b;register long a2 __asm__("a2")=c;register long a3 __asm__("a3")=d;register long a4 __asm__("a4")=e;register long a5 __asm__("a5")=f;register long a7 __asm__("a7")=n;__asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7):"memory");return a0;}
#define MAPCALL __NR_mmap2
#endif
void *memcpy(void*d,const void*s,U n){volatile char*q=d;const volatile char*p=s;while(n--)*q++=*p++;return d;}
void *memset(void*d,int v,U n){volatile char*q=d;while(n--)*q++=v;return d;}
static long openfile(char*p,int flags){return sc(__NR_openat,-100,(long)p,flags,0755,0,0);}
static unsigned char*map(long fd,U n,int prot){long p=sc(MAPCALL,0,n,prot,fd<0?0x22:2,fd,0);return (unsigned long)p>=(unsigned long)-4095?0:(void*)p;}
static U hash(const unsigned char*p,U n){U h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
static U word(const unsigned char*p){return (U)p[0]|((U)p[1]<<8)|((U)p[2]<<16)|((U)p[3]<<24);}
static int expand(const unsigned char*s,U sn,unsigned char*d,U dn){U ip=0,op=0;while(ip<sn){U token=s[ip++],n=token>>4,v;
 if(n==15){do{if(ip>=sn)return -1;v=s[ip++];if(n>dn-v)return -1;n+=v;}while(v==255);}
 if(n>sn-ip || n>dn-op)return -1;for(U j=0;j<n;j++)d[op++]=s[ip++];if(ip==sn)return op==dn?0:-1;
 if(ip+2>sn)return -1;U off=s[ip]|((U)s[ip+1]<<8);ip+=2;if(!off || off>op)return -1;n=token&15;
 if(n==15){do{if(ip>=sn)return -1;v=s[ip++];if(n>dn-v)return -1;n+=v;}while(v==255);}n+=4;
 if(n>dn-op)return -1;for(U j=0;j<n;j++){d[op]=d[op-off];op++;}}
 return op==dn?0:-1;}
static int apply(char*base,char*patch,char*target){long pf=openfile(patch,0),bf=openfile(base,0);U h[10];if(pf<0 || bf<0)return 1;
 if(sc(__NR_read,pf,(long)h,sizeof(h),0,0,0)!=sizeof(h) || h[0]!=0x32504442)return 2;
 if(!h[1] || h[1]>8*1024*1024 || !h[3] || h[3]>8*1024*1024 || h[5]>16*1024*1024 || h[6]>8*1024*1024 || h[7]>200000 || 12*h[7]+h[8]+h[9]!=h[5])return 3;
 unsigned char*p=map(pf,h[6]+40,1),*b=map(bf,h[1],1),*raw=map(-1,h[5],3),*out=map(-1,h[3],3);if(!p||!b||!raw||!out)return 4;
 if(hash(b,h[1])!=h[2] || expand(p+40,h[6],raw,h[5]))return 5;
 U di=0,ei=0,ni=0;long oi=0;unsigned char*diff=raw+12*h[7],*extra=diff+h[8];
 for(U k=0;k<h[7];k++){U a=word(raw+12*k),x=word(raw+12*k+4);int z=(int)word(raw+12*k+8);
  if(a>h[8]-di || a>h[3]-ni)return 6;
  for(U j=0;j<a;j++){long old=oi+(long)j;out[ni++]=(unsigned char)(diff[di++]+(old>=0 && old<h[1]?b[old]:0));}oi+=a;
  if(x>h[9]-ei || x>h[3]-ni)return 7;for(U j=0;j<x;j++)out[ni++]=extra[ei++];oi+=z;
 }
 if(ni!=h[3] || di!=h[8] || ei!=h[9] || hash(out,ni)!=h[4])return 8;
 long fd=openfile(target,0101|01000);if(fd<0)return 9;U off=0;while(off<ni){long n=sc(__NR_write,fd,(long)(out+off),ni-off,0,0,0);if(n<=0)return 10;off+=n;}sc(__NR_close,fd,0,0,0,0,0);sc(__NR_write,1,(long)"PATCH_OK\n",9,0,0,0);return 0;
}
#ifdef HOST
int main(int argc,char**argv){return argc==4?apply(argv[1],argv[2],argv[3]):11;}
#else
int entry(unsigned*sp){return sp[0]==4?apply((char*)sp[2],(char*)sp[3],(char*)sp[4]):11;}
__asm__(".global _start\n_start:\nmv a0,sp\ncall entry\nli a7,93\necall\n");
#endif
