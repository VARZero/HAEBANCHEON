/* SPDX-License-Identifier: GPL-2.0-only
 * RV32 Linux syscall-only storage check. No BusyBox dd/hash applets required.
 * Creates one O_EXCL test file, fsyncs, drops page cache and compares every byte.
 */
typedef unsigned int u32;
typedef unsigned long usize;
void *memcpy(void *dst,const void *src,usize n)
{unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int value,usize n)
{unsigned char *d=dst;while(n--)*d++=value;return dst;}
static long call(long n,long a,long b,long c,long d,long e,long f)
{
 register long a0 __asm__("a0")=a,a1 __asm__("a1")=b,a2 __asm__("a2")=c;
 register long a3 __asm__("a3")=d,a4 __asm__("a4")=e,a5 __asm__("a5")=f;
 register long a7 __asm__("a7")=n;
 __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a4),"r"(a5),"r"(a7):"memory");
 return a0;
}
#define sc(n,a,b,c) call(n,(long)(a),(long)(b),(long)(c),0,0,0)
static usize len(const char *s) {usize n=0;while(s[n])n++;return n;}
static int equal(const char *a,const char *b) {while(*a && *a==*b){a++;b++;}return *a==*b;}
static int full(long fd,void *buf,usize n,int write)
{
 char *p=buf;
 while(n) {long r=sc(write?64:63,fd,p,n);if(r==-4)continue;if(r<=0)return -1;p+=r;n-=r;}
 return 0;
}
static void say(const char *s) {full(1,(void*)s,len(s),1);}
static void number(u32 n) {char s[11];usize i=sizeof(s);do{s[--i]='0'+n%10;n/=10;}while(n);full(1,s+i,sizeof(s)-i,1);}
static int fail(const char *step,long error)
{say("FAIL storage-check ");say(step);say(" error=");if(error<0){say("-");error=-error;}number(error);say("\n");return 1;}
static char buf[4096];
static unsigned char pattern(u32 offset)
{u32 x=offset+0x9e3779b9U;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
static long openfile(const char *p,int flags,int mode) {return call(56,-100,(long)p,flags,mode,0,0);}
static int mounted(void)
{
 long fd=openfile("/proc/mounts",0,0),r;
 if(fd<0)return 0;
 r=sc(63,fd,buf,sizeof(buf)-1);sc(57,fd,0,0);
 if(r<=0)return 0;
 buf[r]=0;
 const char *needle=" /data ext4 rw";
 for(long i=0;i<r;i++) {usize j=0;while(i+(long)j<r && needle[j] && buf[i+j]==needle[j])j++;if(!needle[j])return 1;}
 return 0;
}
int main(int argc,char **argv)
{
 long r,fd=-1;char path[96]="/data/oryukdo-native-check.";
 usize p=len(path);u32 pid=sc(172,0,0,0);char digits[10];usize n=0;
 int verify=argc==3 && equal(argv[1],"--verify");
 if(argc==2 && equal(argv[1],"--identity")) {
  u32 hash=2166136261U,total=0;
  fd=openfile(argv[0],0,0);if(fd<0)return fail("identity open",fd);
  while((r=sc(63,fd,buf,sizeof(buf)))>0) {
   total+=r;for(long i=0;i<r;i++){hash^=(unsigned char)buf[i];hash*=16777619U;}
  }
  sc(57,fd,0,0);if(r<0)return fail("identity read",r);
  say("IDENTITY bytes=");number(total);say(" fnv1a=");number(hash);say("\n");return 0;
 }
 if(argc==2 && equal(argv[1],"--unmount-data")) {
  sc(162,0,0,0);r=sc(39,"/data",0,0);
  if(r<0)return fail("unmount",r);say("PASS unmount /data\n");return 0;
 }
 if(argc==2 && equal(argv[1],"--mount-data")) {
  r=call(40,(long)"/dev/mmcblk0p2",(long)"/data",(long)"ext4",0,0,0);
  if(r<0)return fail("mount",r);say("PASS mount /data\n");return 0;
 }
 if(argc!=1 && !verify)return fail("arguments",-22);
 if(!mounted())return fail("/data is not writable ext4",-19);
 if(verify) {
  usize i;
  for(i=0;i<p;i++)if(argv[2][i]!=path[i])return fail("verify path",-22);
  if(len(argv[2])>=sizeof(path))return fail("verify path length",-22);
  memcpy(path,argv[2],len(argv[2])+1);
 } else {
 do {digits[n++]='0'+pid%10;pid/=10;}while(pid);
 while(n)path[p++]=digits[--n];path[p]=0;
 fd=openfile(path,1|64|128,0600); /* O_WRONLY|O_CREAT|O_EXCL */
 if(fd<0)return fail("create",fd);
 say("WRITE_BEGIN bytes=262144\n");
 for(u32 off=0;off<262144;off+=sizeof(buf)) {
  for(u32 i=0;i<sizeof(buf);i++)buf[i]=pattern(off+i);
  if(full(fd,buf,sizeof(buf),1)) {sc(57,fd,0,0);return fail("write",-5);}
 }
 r=sc(82,fd,0,0);sc(57,fd,0,0);if(r<0)return fail("fsync",r);
 say("WRITE_FSYNC_DONE\n");
 }
 sc(162,0,0,0);
 fd=openfile("/proc/sys/vm/drop_caches",1,0);
 if(fd==-2) {
  /* This minimal kernel has no drop_caches sysctl or fadvise syscall.
   * Only proceed when a normal (not lazy) unmount has succeeded. */
  r=sc(39,"/data",0,0);if(r<0)return fail("unmount for fresh read",r);
  say("UNMOUNT_DONE\n");
  r=call(40,(long)"/dev/mmcblk0p2",(long)"/data",(long)"ext4",0,0,0);
  if(r<0)return fail("remount for fresh read",r);
  say("READ_AFTER_REMOUNT_BEGIN\n");
 } else {
  if(fd<0)return fail("open drop_caches",fd);
  r=sc(64,fd,"3\n",2);sc(57,fd,0,0);if(r!=2)return fail("drop_caches",r);
  say("READ_AFTER_CACHE_DROP_BEGIN\n");
 }
 fd=openfile(path,0,0);if(fd<0)return fail("reopen",fd);
 for(u32 off=0;off<262144;off+=sizeof(buf)) {
  if(full(fd,buf,sizeof(buf),0)) {sc(57,fd,0,0);return fail("read",-5);}
  for(u32 i=0;i<sizeof(buf);i++) if((unsigned char)buf[i]!=pattern(off+i)) {
   say("MISMATCH offset=");number(off+i);say("\n");sc(57,fd,0,0);return 1;
  }
  if((off+sizeof(buf))%65536==0) {say("READ_PROGRESS bytes=");number(off+sizeof(buf));say("\n");}
 }
 r=sc(63,fd,buf,1);sc(57,fd,0,0);if(r!=0)return fail("unexpected file length",r);
 r=sc(35,-100,path,0);if(r<0)return fail("unlink test file",r);
 sc(162,0,0,0);
 say("PASS SD ext4 write/read: bytes=262144 exact_byte_compare\n");
 return 0;
}
__asm__(".global _start\n_start:\n lw a0,0(sp)\n addi a1,sp,4\n andi sp,sp,-16\n call main\n li a7,93\n ecall\n1: j 1b\n");
