#include <asm/unistd.h>
static long sc(long n,long a,long b,long c,long d){register long a0 __asm__("a0")=a;register long a1 __asm__("a1")=b;register long a2 __asm__("a2")=c;register long a3 __asm__("a3")=d;register long a7 __asm__("a7")=n;__asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),"r"(a7):"memory");return a0;}
void *memcpy(void*d,const void*s,unsigned n){char*q=d;const char*p=s;while(n--)*q++=*p++;return d;}
void *memset(void*d,int v,unsigned n){char*q=d;while(n--)*q++=v;return d;}
static unsigned len(const char*p){unsigned n=0;while(p[n])n++;return n;}
static int wr(int fd,const char*p,unsigned n){while(n){long k=sc(__NR_write,fd,(long)p,n,0);if(k<=0)return -1;p+=k;n-=k;}return 0;}
static const char*find(const char*p,const char*q){if(!p)return 0;for(;*p;p++){unsigned i=0;while(q[i] && p[i]==q[i])i++;if(!q[i])return p;}return 0;}
static void text(const char*s){wr(1,s,len(s));}
static void field(const char*name,const char*group,const char*key,int comma){if(comma)text(",");text("\"");text(name);text("\":");const char*p=find(group,key);if(!p){text("null");return;}p+=len(key);while(*p==' '||*p==':')p++;const char*end=p;if(*p=='"'){end++;while(*end && *end!='"')end++;if(*end)end++;}else while(*end && *end!=',' && *end!='}' && *end!='\n')end++;wr(1,p,end-p);}
static char buffer[65536];
int entry(unsigned*stack){int count=8;if(stack[0]>1){count=0;char*p=(char*)stack[2];while(*p>='0'&&*p<='9')count=count*10+*p++-'0';if(count<1||count>100)return 2;}
 struct {unsigned short family;char path[108];} address={1,"/tmp/bosio-wm.sock"};
 for(int i=0;i<count;i++){
  int fd=sc(__NR_socket,1,1,0,0);if(fd<0)return 3;
  if(sc(__NR_connect,fd,(long)&address,sizeof(address),0)<0)return 4;
  const char request[]="{\"id\":1,\"app\":\"dirty-bench\",\"op\":\"get_state\",\"args\":{}}\n";
  if(wr(fd,request,sizeof(request)-1))return 5;
  unsigned n=0;while(n<sizeof(buffer)-1){long got=sc(__NR_read,fd,(long)(buffer+n),sizeof(buffer)-1-n,0);if(got<=0)return 6;n+=got;if(buffer[n-1]=='\n')break;}buffer[n]=0;sc(__NR_close,fd,0,0,0);
  if(!find(buffer,"\"ok\":true"))return 7;
  const char*p=find(buffer,"\"performance\""),*o=find(buffer,"\"output\""),*t=find(buffer,"\"texture_pool\""),*d=find(buffer,"\"memory_dma\""),*s=find(buffer,"\"service\"");
  text("BENCH_SAMPLE {");field("compose_ms",p,"\"last_compose_ms\"",0);field("renders",p,"\"render_count\"",1);field("uptime",p,"\"uptime_seconds\"",1);field("generation",p,"\"rendered_generation\"",1);field("commits",o,"\"commits\"",1);field("dma_errors",d,"\"errors\"",1);field("dma_upload_bytes",d,"\"upload_bytes\"",1);field("pool_upload_bytes",t,"\"upload_bytes\"",1);field("partial_uploads",t,"\"partial_uploads\"",1);field("full_uploads",t,"\"full_uploads\"",1);field("phase",s,"\"phase\"",1);field("snapshot_age",s,"\"snapshot_age_seconds\"",1);field("render_error",buffer,"\"render_error\"",1);field("app_error",buffer,"\"app_error\"",1);text("}\n");
  if(i+1<count){long long ts[2]={2,0};sc(__NR_clock_nanosleep_time64,1,0,(long)ts,0);}
 }
 return 0;
}
__asm__(".global _start\n_start:\nmv a0,sp\ncall entry\nli a7,93\necall\n");
