// Uses the original BoAYO Client with optional native-word shared images.
#include "ipc.hpp"
#include "content.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <chrono>
#include <csignal>
#include <iostream>
using namespace boayo;
static volatile sig_atomic_t done=0;
static std::string source_format="RGB332";
static void stop_app(int) { done=1; }
static double clock_ms() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct AppSurface {
  int id,fd; unsigned sequence=0,width,height; size_t bytes; uint8_t *pixels;
  Surface image; IndexedSurface indexed;
  AppSurface(Client &client,double yaw=0,double pitch=0,unsigned color=84) {
    id=client.call("create_window",{{"title","BoAYO triangle application"},{"surface_width",240},{"surface_height",144},
      {"width_deg",20},{"height_deg",14},{"azimuth",yaw},{"elevation",pitch},{"background_index",color}}).at("window_id");
    auto info=client.call("create_surface",{{"window_id",id},{"format",source_format}});
    width=info.at("width"); height=info.at("height"); bytes=info.at("slot_bytes");
    fd=open(info.at("path").get<std::string>().c_str(),O_RDWR|O_CLOEXEC); require(fd>=0,"open shared surface");
    pixels=static_cast<uint8_t*>(mmap(nullptr,bytes*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
    require(pixels!=MAP_FAILED,"map shared surface");
    if(source_format=="RGB332")indexed.init(width,height,color);else image.init(width,height,color);
    put({{"id",1},{"type","text"},{"x",18},{"y",14},{"scale",2},{"text","BOAYO APP"}});
    put({{"id",2},{"type","text"},{"x",18},{"y",45},{"scale",2},{"text",source_format=="RGB332"?"SHARED RGB332":source_format=="XRGB8888"?"SHARED RGB32":"SHARED RGB24"}});
    put({{"id",3},{"x",18},{"y",88},{"w",160},{"h",14},{"color",228}});
  }
  ~AppSurface() { munmap(pixels,bytes*2); close(fd); }
  DamageRect put(const J &c){return source_format=="RGB332"?indexed.put(c):image.put(c);}
  J commit(Client &client,DamageRect r) {
    unsigned slot=sequence%2;
    if(source_format=="RGB332") {
      for(int y=r.y;y<r.y+r.h;y++) std::memcpy(pixels+slot*bytes+size_t(y)*width+r.x,
        indexed.pixels.data()+size_t(y)*width+r.x,size_t(r.w));
    } else if(source_format=="XRGB8888") {
      for(int y=r.y;y<r.y+r.h;y++) std::memcpy(pixels+slot*bytes+(size_t(y)*width+r.x)*4,
        image.pixels.data()+size_t(y)*width+r.x,size_t(r.w)*4);
    } else for(int y=r.y;y<r.y+r.h;y++) for(int x=r.x;x<r.x+r.w;x++) {
      uint32_t color=image.pixels[size_t(y)*width+x]; auto *rgb=pixels+slot*bytes+(size_t(y)*width+x)*3;
      rgb[0]=color>>16; rgb[1]=color>>8; rgb[2]=color;
    }
    return client.call("commit_surface",{{"window_id",id},{"sequence",++sequence},{"slot",slot},{"damage",{{r.x,r.y,r.w,r.h}}}});
  }
};
static void wait(Client &client,const J &result) { client.call("wait_present",{{"generation",result.at("generation")}}); }
template<class F> static void measure(Client &client,const std::string &name,F action) {
  double start=clock_ms(); auto result=action(); double accepted=clock_ms(); wait(client,result); double end=clock_ms();
  auto state=client.call("get_state");
  std::cout<<"BOAYO_TRIANGLE_SAMPLE "<<J{{"name",name},{"accept_ms",accepted-start},{"present_ms",end-start},
    {"result",result},{"render",state.at("performance").at("last_render")}}.dump()<<std::endl;
}
int main(int argc,char **argv) {
  try {
    signal(SIGPIPE,SIG_IGN); signal(SIGTERM,stop_app); signal(SIGINT,stop_app);
    Client app("triangle-app");
    bool demo=false,formats_only=false;double yaw=0;
    for(int i=1;i<argc;i++) {std::string arg=argv[i];if(arg=="--demo")demo=true;else if(arg=="--formats-only")formats_only=true;else if(arg=="--rgb24")source_format="RGB24";else if(arg=="--rgb32")source_format="XRGB8888";else if(arg=="--rgb332")source_format="RGB332";else yaw=std::stod(arg);}
    std::cout<<"BOAYO_TRIANGLE_FORMAT "<<(source_format)<<std::endl;
    double start=clock_ms(); AppSurface surface(app,yaw);
    wait(app,J{{"generation",app.call("sync_scene")}});
    std::cout<<"BOAYO_TRIANGLE_CREATE "<<J{{"window_id",surface.id},{"create_and_map_ms",clock_ms()-start}}.dump()<<std::endl;
    measure(app,"initial_content",[&]{return surface.commit(app,{0,0,240,144});});
    if(demo) {
      unsigned n=0;
      while(!done) {
        auto state=app.call("get_state"); bool exists=false;
        for(auto &w:state.at("windows")) if(w.at("window_id")==surface.id) exists=true;
        if(!exists) break;
        auto d=surface.put({{"id",4},{"type","text"},{"x",18},{"y",115},{"scale",2},
          {"text",std::string("FRAME ")+char('0'+(n/10)%10)+char('0'+n%10)}});
        if(d.w) measure(app,"demo_update",[&]{return surface.commit(app,d);});
        n++; for(unsigned i=0;i<20 && !done;i++) usleep(100000);
      }
      return 0;
    }
    for(int n=0;n<11;n++) {
      auto d=surface.put({{"id",5},{"x",20},{"y",66},{"w",8},{"h",8},{"color",n%2?38:255}});
      measure(app,"partial_8x8",[&]{return surface.commit(app,d);});
    }
    if(formats_only) {
      measure(app,"destroy_owned",[&]{return app.call("destroy_window",{{"window_id",surface.id}});});
      std::cout<<"BOAYO_TRIANGLE_BENCH_DONE"<<std::endl;return 0;
    }
    Client control("internal-benchmark",socket_path()+".control");
    for(int n=0;n<7;n++) measure(control,"content_move",[&]{return control.call("configure_window",{{"window_id",surface.id},{"azimuth",n%2?5:-5},{"elevation",n%2?2:-2}});});
    for(int n=0;n<7;n++) measure(control,"content_resize",[&]{return control.call("configure_window",{{"window_id",surface.id},{"width_deg",n%2?20:24},{"height_deg",n%2?14:16}});});
    measure(control,"content_restore_size",[&]{return control.call("configure_window",{{"window_id",surface.id},{"azimuth",0},{"elevation",0},{"width_deg",20},{"height_deg",14}});});
    AppSurface second(app,8,3,164), third(app,-9,-3,109);
    measure(app,"second_content",[&]{return second.commit(app,{0,0,240,144});});
    measure(app,"third_content",[&]{return third.commit(app,{0,0,240,144});});
    for(int n=0;n<5;n++) measure(control,"overlap_move",[&]{return control.call("configure_window",{{"window_id",third.id},{"azimuth",n%2?-5:-12}});});
    measure(control,"raise",[&]{return control.call("raise_window",{{"window_id",surface.id}});});
    measure(control,"close",[&]{return control.call("destroy_window",{{"window_id",third.id}});});
    measure(control,"close",[&]{return control.call("destroy_window",{{"window_id",second.id}});});
    for(int n=0;n<7;n++) {
      double t=clock_ms(); auto r=control.call("view_set",{{"yaw",n%2?5:-5},{"pitch",n%2?2:-2}});
      std::cout<<"BOAYO_TRIANGLE_SAMPLE "<<J{{"name","view_pose"},{"present_ms",clock_ms()-t},{"result",r}}.dump()<<std::endl;
    }
    auto sweep=control.call("pose_sweep",{{"samples",61},{"amplitude",8}});
    std::cout<<"BOAYO_TRIANGLE_POSE_SWEEP "<<sweep.dump()<<std::endl;
    measure(app,"destroy_owned",[&]{return app.call("destroy_window",{{"window_id",surface.id}});});
    std::cout<<"BOAYO_TRIANGLE_BENCH_DONE"<<std::endl;
  } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
}
