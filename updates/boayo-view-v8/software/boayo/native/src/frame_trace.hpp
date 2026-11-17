#pragma once
#include "init_trace.hpp"
namespace boayo::frame_trace {
inline thread_local bool active=false;
inline thread_local unsigned sequence=0;
inline thread_local long long wall_start=0,cpu_start=0;
inline thread_local int uart=-1;
inline void mark(const char *event,long long value=0){
 if(!active || sequence>=64)return;int saved=errno;
 char line[224];auto w=init_trace::clock_us(CLOCK_MONOTONIC),c=init_trace::clock_us(CLOCK_THREAD_CPUTIME_ID);
 int n=std::snprintf(line,sizeof(line),"BOAYO_FRAME_TRACE n=%u event=%s wall_us=%lld cpu_us=%lld value=%lld\n",++sequence,event,w-wall_start,c-cpu_start,value);
 if(n>0 && n<int(sizeof(line))){(void)!write(STDERR_FILENO,line,n);if(uart>=0)(void)!write(uart,line,n);}errno=saved;
}
struct Scope{
 bool owner=false;
 explicit Scope(bool first){const char *v=getenv("BOAYO_INIT_TRACE");if(!first || active || !v || std::strcmp(v,"1"))return;
  owner=active=true;sequence=0;wall_start=init_trace::clock_us(CLOCK_MONOTONIC);cpu_start=init_trace::clock_us(CLOCK_THREAD_CPUTIME_ID);
  const char *console=getenv("BOAYO_INIT_TRACE_UART");if(console && !std::strcmp(console,"1"))uart=open("/dev/console",O_WRONLY|O_CLOEXEC);mark("frame.begin");}
 ~Scope(){if(!owner)return;mark("frame.scope_exit");if(uart>=0)close(uart);uart=-1;active=false;}
};
}
