// Native request-to-wait_present benchmark; UART is outside the timed interval.
#include "ipc.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <chrono>
#include <iostream>
using namespace boayo;
static double ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void send_all(Client &c,const std::string &bytes){
  size_t off=0;while(off<bytes.size()){auto n=send(c.sock.fd,bytes.data()+off,bytes.size()-off,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;require(n>0,"burst write");off+=n;}
}
static J receive(Client &c,unsigned id){
  while(c.buffer.find('\n')==std::string::npos){char b[8192];auto n=recv(c.sock.fd,b,sizeof(b),0);if(n<0&&errno==EINTR)continue;require(n>0,"burst read");c.buffer.append(b,n);require(c.buffer.size()<1024*1024,"reply bound");}
  auto end=c.buffer.find('\n');auto r=J::parse(c.buffer.substr(0,end));c.buffer.erase(0,end+1);
  require(r.at("id")==id&&r.at("ok").get<bool>(),"burst rejected");return r.at("result");
}
int main(){try{
  Client app("coalescing-benchmark");
  auto created=app.call("create_window",{{"surface_width",240},{"surface_height",144},{"width_deg",20},{"height_deg",14},{"background_index",84}});
  int id=created.at("window_id");app.call("wait_present",{{"generation",created.at("generation")}});
  auto info=app.call("create_surface",{{"window_id",id},{"format","RGB332"}});
  FD fd(open(info.at("path").get<std::string>().c_str(),O_RDWR));require(fd.fd>=0,"surface open");size_t size=info.at("slot_bytes");
  auto *pixels=static_cast<uint8_t*>(mmap(nullptr,size*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd.fd,0));require(pixels!=MAP_FAILED,"surface mmap");
  std::memset(pixels,84,size*2);unsigned sequence=1;
  auto initial=app.call("commit_surface",{{"window_id",id},{"slot",0},{"sequence",sequence},{"damage",{{0,0,240,144}}}});
  app.call("wait_present",{{"generation",initial.at("generation")}});
  std::memset(pixels,228,size);std::memset(pixels+size,31,size);
  for(unsigned i=0;i<8;i++){
    double start=ms();auto r=app.call("commit_surface",{{"window_id",id},{"slot",i%2},{"sequence",++sequence},{"damage",{{20,20,8,8}}}});double accepted=ms();
    app.call("wait_present",{{"generation",r.at("generation")}});double done=ms();
    std::cout<<"COALESCE_SAMPLE "<<J{{"name","single_8x8"},{"accept_ms",accepted-start},{"present_ms",done-start}}.dump()<<std::endl;
  }
  // 128 genuinely disjoint 4x4 rectangles, not artificial JSON padding.
  J damage=J::array();for(int y=0;y<8;y++)for(int x=0;x<16;x++)damage.push_back({16+x*12,16+y*12,4,4});
  for(unsigned round=0;round<3;round++){
    auto before=app.call("get_state").at("performance");std::string wire;unsigned first=app.sequence+1;
    for(unsigned i=0;i<16;i++)wire+=J{{"id",++app.sequence},{"op","commit_surface"},{"args",{{"window_id",id},{"slot",i%2},{"sequence",++sequence},{"damage",damage}}}}.dump()+"\n";
    double start=ms(),first_ack=0;send_all(app,wire);J last;
    for(unsigned i=0;i<16;i++){last=receive(app,first+i);if(!i)first_ack=ms();}
    double accepted=ms();app.call("wait_present",{{"generation",last.at("generation")}});double done=ms();auto after=app.call("get_state").at("performance");
    J result={{"name","burst_16x128_rects"},{"round",round+1},{"submissions",16},{"wire_bytes",wire.size()},{"first_ack_ms",first_ack-start},{"accept_ms",accepted-start},{"present_ms",done-start}};
    if(after.contains("publications")){result["publications"]=after.at("publications").get<uint64_t>()-before.at("publications").get<uint64_t>();result["coalesced_updates"]=after.at("coalesced_updates").get<uint64_t>()-before.at("coalesced_updates").get<uint64_t>();result["last_content_batch"]=after.at("last_content_batch");}
    std::cout<<"COALESCE_SAMPLE "<<result.dump()<<std::endl;
  }
  auto closed=app.call("destroy_window",{{"window_id",id}});app.call("wait_present",{{"generation",closed.at("generation")}});munmap(pixels,size*2);
  std::cout<<"COALESCE_BENCH_DONE"<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
