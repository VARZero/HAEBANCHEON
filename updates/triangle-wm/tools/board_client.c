/* Small target harness: no Python startup and no UART in the timed interval. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))exit(2);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char**argv){
 if(argc!=2)return 2;
 FILE*f=fopen(argv[1],"r");if(!f){perror("requests");return 2;}char request[16384],answer[65536];unsigned index=0;
 while(fgets(request,sizeof(request),f)){
  if(request[0]=='#'){unsigned ms=0;if(sscanf(request,"#wait %u",&ms)==1){struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);}else{fputs(request,stdout);fflush(stdout);}continue;}
  size_t size=strlen(request);if(!size||request[size-1]!='\n'){fprintf(stderr,"unterminated request\n");return 2;}request[size-1]=0;
  int s=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={0};a.sun_family=AF_UNIX;strcpy(a.sun_path,"/tmp/triangle-wm.sock");if(s<0||connect(s,(void*)&a,sizeof(a))){perror("connect");return 2;}
  request[size-1]='\n';double start=now();size_t sent=0;while(sent<size){ssize_t n=write(s,request+sent,size-sent);if(n<=0){perror("send");return 2;}sent+=n;}
  size_t got=0;while(got<sizeof(answer)-1){ssize_t n=read(s,answer+got,sizeof(answer)-1-got);if(n<=0){if(n<0&&errno==EINTR)continue;break;}got+=n;if(answer[got-1]=='\n')break;}
  double elapsed=(now()-start)*1000;close(s);answer[got]=0;request[size-1]=0;
  if(!got||answer[got-1]!='\n'){fprintf(stderr,"incomplete response\n");return 2;}
  answer[got-1]=0;printf("TRIANGLE_SAMPLE {\"index\":%u,\"ipc_ms\":%.6f,\"response\":%s}\n",index++,elapsed,answer);fflush(stdout);
  if(strstr(answer,"\"error\":"))return 1;
 }
 fclose(f);puts("TRIANGLE_SUITE_DONE");return 0;
}
