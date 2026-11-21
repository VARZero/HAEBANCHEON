// BoAYO's application protocol, with an independent triangle-tile WM.
#define TRIANGLE_ENGINE_ONLY
#include "triangle_wm.cpp"
#include "ipc.hpp"
#include "application_buffer.hpp"
#include "pending_content.hpp"
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <climits>

namespace triangle_service {
static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }
static int integer(const J &a, const char *key, int fallback, int lo, int hi) {
  if (!a.contains(key)) return fallback;
  require(a.at(key).is_number_integer(), std::string(key)+" must be integer");
  int64_t n=a.at(key).get<int64_t>();
  require(n>=lo && n<=hi, std::string(key)+" out of bounds");
  return int(n);
}
struct Record {
  int owner; std::string title; unsigned width, height;
  std::unique_ptr<ApplicationBuffer> shared;
  PendingContent pending;
};
struct Peer {
  int fd; bool control; std::string in, out;
  // One outstanding wait per connection, just like the synchronous app Client.
  J wait_id; uint64_t wait_generation=0;bool feedback_wait=false;
};
struct RequestProfile {
 std::string op;uint64_t parse_us=0,dispatch_us=0,reply_us=0,snapshot_us=0,pixels_us=0,damage_us=0,footprint_us=0;
 J report()const{return json_object("operation",op,"parse_us",parse_us,"dispatch_us",dispatch_us,"reply_us",reply_us,"snapshot_us",snapshot_us,"pixels_us",pixels_us,"damage_us",damage_us,"footprint_us",footprint_us);}
};
struct Server {
  std::string path;
  FD lock, listener, control;
  std::unique_ptr<Engine> engine;
  std::map<int,Peer> peers;
  std::map<int,Record> records;
  uint64_t generation=0, presented=0, feedback=0;
  int next_id=1, focused=0, next_app_fd=0;
  size_t reserved_bytes=0;
  bool dirty=false,scene_dirty=true;
  uint64_t content_batches=0,content_updates=0,coalesced_updates=0;
  J last_content_batch=J::object();
  uint64_t shell_publications=0;J last_shell_render=J::object();
  double service_render_ms=0;RequestProfile request_profile,last_mutation;
  double yaw=0, pitch=0;
#include "ui_input.inc"

  explicit Server(bool mock):path(socket_path()) {
    lock.fd=open((path+".lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    require(lock.fd>=0 && flock(lock.fd,LOCK_EX|LOCK_NB)==0,"WM already running or lock unavailable");
    engine=std::make_unique<Engine>(mock);
    engine->ui_enabled=!(getenv("TRIANGLE_INPUT")&&std::string(getenv("TRIANGLE_INPUT"))=="0");
    for(unsigned c:{84u,164u,109u,38u,228u,255u}) engine->solid(c);
    listener.fd=listen_at(path); control.fd=listen_at(path+".control");
  }
  int listen_at(const std::string &p) {
    auto a=address(p);
    struct stat existing{};
    if(lstat(p.c_str(),&existing)==0) {
      require(S_ISSOCK(existing.st_mode),"refusing to replace a non-socket path");
      FD probe(socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0));
      require(probe.fd>=0,"socket probe failed");
      int rc=connect(probe.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a));
      require(rc<0 && errno==ECONNREFUSED,"socket already has a live owner");
      require(unlink(p.c_str())==0,"stale socket removal failed");
    } else require(errno==ENOENT,"cannot inspect socket path");
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    require(fd>=0,"socket creation failed");
    // The held flock serializes recovery; never replace a live or non-socket path.
    if(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)) || chmod(p.c_str(),0600) || listen(fd,32)) {
      close(fd); throw std::runtime_error("bind/listen failed");
    }
    return fd;
  }
  ~Server() {
    for(auto &[fd,p]:peers) close(fd);
    if(listener.fd>=0) unlink(path.c_str());
    if(control.fd>=0) unlink((path+".control").c_str());
  }
  void changed(bool layout=true,bool scene=true) { ++generation; dirty=true;scene_dirty|=scene;engine->ui_dirty=true;if(layout){engine->ui_layout_dirty=true;engine->ui_mask_dirty=true;} }
  Record &owned(Peer &p,const J &a) {
    int id=integer(a,"window_id",0,1,INT_MAX);
    auto it=records.find(id); require(it!=records.end(),"window not found");
    require(p.control || it->second.owner==p.fd,"window belongs to another connection");
    return it->second;
  }
  // JSON numbers retain their value; integral coordinates avoid costly dtoa
  // on this soft-float CPU. Fractional coordinates remain double precision.
  static J coordinate(double value){int n=int(value);return value==n?J(n):J(value);}
  J description(int id) {
    const auto &r=records.at(id); const auto &w=engine->windows.at(id);
    return json_object("window_id",id,"title",r.title,"surface_width",r.width,"surface_height",r.height,
      "azimuth",coordinate(w.yaw),"elevation",coordinate(w.pitch),"roll",0,"width_deg",coordinate(w.width),"height_deg",coordinate(w.height),
      "padding_deg",coordinate(w.padding),"background_index",w.color,"splash_index",w.splash_color,"phase",w.splash_pending?"splash":(w.shell_pending||!w.plan_ready)?"background":"content","mapped",w.visible,"decorated",false,"generation",generation);
  }
  void erase(int id) {
    if(capture.id==id)cancel_input();
    const auto &r=records.at(id); reserved_bytes-=size_t(r.width)*r.height*(r.shared?4+3*r.shared->pixel_bytes:13);
    records.erase(id); engine->windows.erase(id);
    auto &order=engine->order; order.erase(std::remove(order.begin(),order.end(),id),order.end());
    engine->texture_cache.erase(id);
    if(focused==id) focused=order.empty()?0:order.back();
    changed();
  }
  void disconnect(int fd) {
    if(capture.owner==fd){cancel_input();changed();}
    std::vector<int> gone; for(const auto &[id,r]:records) if(r.owner==fd) gone.push_back(id);
    for(int id:gone) erase(id);
    close(fd); peers.erase(fd);
  }
  void reply(Peer &p,const J &id,const J &result) {
    // Serialize before touching the output queue, so encoding errors remain atomic.
    auto body=result.dump(),identifier=id.dump();
    p.out.reserve(p.out.size()+body.size()+identifier.size()+32);
    p.out+="{\"id\":";p.out+=identifier;p.out+=",\"ok\":true,\"result\":";p.out+=body;p.out+="}\n";
  }
  J dispatch(Peer &p,const std::string &op,const J &a) {
    require(a.is_object(),"args must be an object");
    if(op.rfind("input_",0)==0)return input(p,op,a);
    if(op=="create_window") {
      require(!p.control,"create windows using an application connection");
      require(records.size()<64,"window limit");
      unsigned sw=integer(a,"surface_width",320,1,1024), sh=integer(a,"surface_height",200,1,1024);
      size_t bytes=size_t(sw)*sh*13;
      require(reserved_bytes+bytes<=64*1024*1024,"surface memory budget");
      Window w{}; w.id=next_id; w.yaw=a.value("azimuth",0.); w.pitch=a.value("elevation",0.);
      w.width=a.value("width_deg",34.); w.height=a.value("height_deg",24.);
      w.padding=a.value("padding_deg",2.); w.visible=a.value("mapped",true);
      w.color=integer(a,"background_index",84,0,255);
      w.splash_color=integer(a,"splash_index",228,0,255);require(engine->colors.count(w.splash_color),"splash_index must be reserved");w.shell_pending=true;w.splash_pending=true;
      require(engine->colors.count(w.color),"background_index must be 84,164,109,38,228,255");
      require(a.value("roll",0.)==0.,"window roll is not supported");
      Engine::validate(w);auto phase=microseconds_now();w.footprint=engine->footprint(w);request_profile.footprint_us=microseconds_now()-phase;
      std::string title=a.value("title",std::string("Application")); require(title.size()<=256,"title too long");
      records.emplace(next_id,Record{p.fd,title,sw,sh,{},{}});
      engine->windows.emplace(next_id,std::move(w)); engine->order.push_back(next_id);
      reserved_bytes+=bytes; focused=next_id; changed(); return description(next_id++);
    }
    if(op=="get_state") {
      auto last_render=engine->render_report();last_render["service_render_ms"]=service_render_ms;last_render["request_profile"]=last_mutation.report();
      J list=J::array(); for(int id:engine->order) list.push_back(description(id));
      return {{"windows",list},{"generation",generation},{"focused_window",focused},
        {"service",{{"phase","ready"},{"renderer","triangle"},{"mock",engine->mock}}},
        {"performance",{{"rendered_generation",presented},{"feedback_generation",feedback},{"shell_publications",shell_publications},{"last_shell_render",last_shell_render},{"publications",engine->commits},{"content_batches",content_batches},{"content_updates",content_updates},{"coalesced_updates",coalesced_updates},{"last_content_batch",last_content_batch},{"last_render",last_render}}}};
    }
    if(op=="app_events" || op=="poll_events")
      return {{"events",J::array()},{"focused_window",focused},{"view",{yaw,pitch,0}},
        {"apps",J::array()},{"stored",J::array()},{"generation",generation}};
    if(op=="sync_scene") return generation;
    if(op=="view_set") {
      require(p.control,"view_set requires the internal control connection");
      double y=a.value("yaw",0.), pi=a.value("pitch",0.);
      require(a.value("roll",0.)==0.,"view roll is not supported");
      require(std::isfinite(y)&&std::abs(y)<=180&&std::isfinite(pi)&&std::abs(pi)<=85,"view bounds");
      if(capture.owner>=0)cancel_input();
      apply_pending_content();
      if(engine->active<0)present();
      auto result=engine->operate({{"op","pose"},{"yaw",y},{"pitch",pi}});
      yaw=y; pitch=pi; result["yaw"]=yaw; result["pitch"]=pitch; result["roll"]=0; return result;
    }
    if(op=="pose_sweep") {
      require(p.control && engine->active>=0,"pose_sweep requires internal control and a scene");
      apply_pending_content();auto q=a; q["op"]="pose_sweep"; auto result=engine->operate(q); yaw=pitch=0; return result;
    }
    if(op=="snapshot") { require(p.control,"internal operation"); return engine->snapshot(); }
    if(op=="shutdown") { require(p.control,"internal operation"); stopping=1; return {{"stopping",true}}; }

    auto &r=owned(p,a); int id=a.at("window_id"); auto &w=engine->windows.at(id);
    if(op=="create_surface") {
      std::string format=a.value("format",r.shared?r.shared->format:std::string("RGB24"));
      require(format=="RGB24" || format=="XRGB8888" || format=="RGB332","unsupported surface format");
      if(r.shared) require(r.shared->format==format,"surface format is immutable");
      else {
        size_t old_charge=size_t(r.width)*r.height*13;
        size_t charge=size_t(r.width)*r.height*(4+3*(format=="RGB332"?1:format=="XRGB8888"?4:3));
        require(reserved_bytes-old_charge+charge<=64*1024*1024,"surface memory budget");
        auto shared=std::make_unique<ApplicationBuffer>(r.width,r.height,format,source_color(w.color));
        r.shared=std::move(shared);reserved_bytes=reserved_bytes-old_charge+charge;
      }
      auto result=r.shared->description(); result["window_id"]=id; return result;
    }
    if(op=="commit_surface") {
      require(bool(r.shared),"create_surface first");auto phase=microseconds_now();auto patches=r.shared->snapshot(a);request_profile.snapshot_us=microseconds_now()-phase;phase=microseconds_now();
      auto &s=w.surface;
      if(!w.has_content) {
        s.width=r.width; s.height=r.height; s.bg=w.color;
        s.pixels.assign(size_t(r.width)*r.height,source_color(w.color));
      }
      DamageRect total{}; std::vector<DamageRect> areas; size_t copied=0;
      for(const auto &patch:patches) {
        auto area=patch.area; bool different=false;
        // Copy before acknowledging the slot lease. Conversion is deferred until
        // publication, so overlapping queued submissions convert only their latest pixels.
        for(int y=0;y<area.h;y++) {
          size_t bytes=size_t(area.w)*r.shared->pixel_bytes;
          const auto *src=patch.rgb.data()+size_t(y)*bytes;
          auto *dst=r.shared->latest.data()+(size_t(area.y+y)*r.width+area.x)*r.shared->pixel_bytes;
          if(std::memcmp(dst,src,bytes)){std::memcpy(dst,src,bytes);different=true;}
        }
        if(different) { DamageRect d{area.x,area.y,area.w,area.h}; total=unite(total,d); areas.push_back(d); }
        copied+=patch.rgb.size();
      }
      request_profile.pixels_us=microseconds_now()-phase;phase=microseconds_now();
      bool did_change=!w.has_content || total.w;
      if(did_change) {
        bool first_content=!w.has_content;
        if(!w.shell_pending)w.splash_pending=false;
        if(!w.has_content) { total={0,0,int(r.width),int(r.height)}; w.plan_ready=false; }
        w.has_content=true; s.pending=unite(s.pending,total);
        r.pending.merge(areas);
        ++r.pending.commits;
        changed(first_content);
      }
      request_profile.damage_us=microseconds_now()-phase;
      r.shared->sequence=a.at("sequence").get<uint64_t>(); r.shared->commits++; r.shared->copied_bytes+=copied;
      return json_object("window_id",id,"sequence",r.shared->sequence,"generation",generation,"changed",did_change,"copied_bytes",copied);
    }
    if(op=="configure_window") {
      // Validate all changes before touching the live window or its shared mapping.
      require(!a.contains("surface_width") || a.at("surface_width")==r.width,"source resolution is immutable; create a new window");
      require(!a.contains("surface_height") || a.at("surface_height")==r.height,"source resolution is immutable; create a new window");
      require(a.value("roll",0.)==0.,"window roll is not supported");
      Window bounds{}; bounds.yaw=a.value("azimuth",w.yaw); bounds.pitch=a.value("elevation",w.pitch);
      bounds.width=a.value("width_deg",w.width); bounds.height=a.value("height_deg",w.height);
      bounds.padding=a.value("padding_deg",w.padding); Engine::validate(bounds);
      bool visible=a.value("mapped",w.visible);
      std::string title=a.value("title",r.title); require(title.size()<=256,"title too long");
      bool geometry=bounds.yaw!=w.yaw || bounds.pitch!=w.pitch || bounds.width!=w.width || bounds.height!=w.height || bounds.padding!=w.padding;
      if(capture.id==id&&(geometry||visible!=w.visible))cancel_input();
      if(geometry || visible!=w.visible || title!=r.title) {
        if(geometry) { w.yaw=bounds.yaw; w.pitch=bounds.pitch; w.width=bounds.width; w.height=bounds.height; w.padding=bounds.padding;
          auto phase=microseconds_now();w.footprint=engine->footprint(w);request_profile.footprint_us=microseconds_now()-phase;w.plan_ready=false; }
        w.visible=visible; r.title=title; changed();
      }
      return description(id);
    }
    if(op=="focus_window" || op=="raise_window") {
      if(engine->order.back()!=id) { auto &order=engine->order; order.erase(std::find(order.begin(),order.end(),id)); order.push_back(id); changed(); }
      focused=id; return description(id);
    }
    if(op=="destroy_window") { erase(id); return {{"window_id",id},{"generation",generation}}; }
    throw std::runtime_error("unsupported operation: "+op);
  }
  void apply_pending_content() {
    uint64_t updates=0,windows=0,regions=0,pixels=0,invalidated=0;
    auto start=microseconds_now();
    static const auto lookup=[](){std::array<uint32_t,256> t{};for(unsigned i=0;i<256;i++)t[i]=source_color(i);return t;}();
    for(auto &[id,r]:records)if(r.pending.commits) {
      auto &w=engine->windows.at(id);auto &s=w.surface;auto &source=*r.shared;
      updates+=r.pending.commits;++windows;regions+=r.pending.regions.size();
      for(auto a:r.pending.regions)for(int y=a.y;y<a.y+a.h;y++) {
        const auto *src=source.latest.data()+(size_t(y)*r.width+a.x)*source.pixel_bytes;
        auto *dst=s.pixels.data()+size_t(y)*r.width+a.x;pixels+=a.w;
        if(source.pixel_bytes==4)std::memcpy(dst,src,size_t(a.w)*4);
        else for(int x=0;x<a.w;x++)dst[x]=source.pixel_bytes==1?lookup[src[x]]:
          (uint32_t(src[x*3])<<16)|(uint32_t(src[x*3+1])<<8)|src[x*3+2];
      }
      // Each affected tile is invalidated once, regardless of queued patch count.
      if(w.plan_ready)for(auto &[tile,plan]:w.plan) {
        if(std::any_of(r.pending.regions.begin(),r.pending.regions.end(),[&](DamageRect a){return affected(plan.descriptor,a);})) {
          plan.version=++engine->serial;++invalidated;
        }
      }
      s.revision=++engine->serial;r.pending.regions.clear();r.pending.commits=0;
    }
    if(updates) {
      ++content_batches;content_updates+=updates;coalesced_updates+=updates-windows;
      last_content_batch=json_object("updates",updates,"windows",windows,"regions",regions,
        "converted_pixels",pixels,"invalidated_tiles",invalidated,"prepare_us",microseconds_now()-start);
    }
  }
  void request(Peer &p,const std::string &line) {
    J id=nullptr;auto started=microseconds_now();request_profile={};
    try {
      auto q=J::parse(line); id=q.at("id"); require(id.is_number_integer(),"integer request id required");
      require(p.wait_id.is_null(),"wait_present already pending");
      const auto &op=q.at("op").get_ref<const std::string&>();static const J empty=J::object();const J &a=q.contains("args")?q.at("args"):empty;
      request_profile.parse_us=microseconds_now()-started;
      if(op=="wait_present"||op=="wait_feedback") {
        require(a.at("generation").is_number_integer(),"integer generation required");
        int64_t gen=a.at("generation"); require(gen>=0 && uint64_t(gen)<=generation,"unknown future generation");
        bool early=op=="wait_feedback";uint64_t ready=early?feedback:presented;
        if(uint64_t(gen)>ready) { p.wait_id=id; p.wait_generation=gen;p.feedback_wait=early; }
        else reply(p,id,json_object("generation",ready,"presented",!early,"feedback",early));
      } else {
        auto phase=microseconds_now();auto result=dispatch(p,op,a);request_profile.dispatch_us=microseconds_now()-phase;phase=microseconds_now();
        reply(p,id,result);request_profile.reply_us=microseconds_now()-phase;
        if(op=="commit_surface"||op=="configure_window"||op=="create_window"||op=="destroy_window"||op=="raise_window"){
          request_profile.op=op;last_mutation=std::move(request_profile);
        }
      }
    } catch(const std::exception &e) { p.out+=J{{"id",id},{"ok",false},{"error",e.what()}}.dump()+"\n"; }
  }
  void flush() {
    std::vector<int> gone;
    for(auto &[fd,p]:peers) {
      if(p.out.size()>1024*1024) { gone.push_back(fd); continue; }
      while(!p.out.empty()) {
        auto n=send(fd,p.out.data(),p.out.size(),MSG_NOSIGNAL);
        if(n>0) p.out.erase(0,n);
        else if(n<0 && errno==EINTR) continue;
        else { if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK)) gone.push_back(fd); break; }
      }
    }
    for(int fd:gone) disconnect(fd);
  }
  bool pump(bool nonblocking,unsigned &budget) {
    std::vector<pollfd> fds;fds.reserve(peers.size()+2);
    fds.push_back({listener.fd,POLLIN,0});fds.push_back({control.fd,POLLIN,0});
    bool buffered=false;
    // Visit control connections first on every sweep; app bursts cannot consume
    // the whole request budget before an already-ready input connection is read.
    auto add=[&](int fd,Peer &p){
      fds.push_back({fd,short(POLLIN|(p.out.empty()?0:POLLOUT)),0});
      buffered|=p.in.find('\n')!=std::string::npos;
    };
    for(auto &[fd,p]:peers)if(p.control)add(fd,p);
    for(auto &[fd,p]:peers)if(!p.control&&fd>=next_app_fd)add(fd,p);
    for(auto &[fd,p]:peers)if(!p.control&&fd<next_app_fd)add(fd,p);
    int rc=poll(fds.data(),fds.size(),nonblocking||buffered?0:100);
    if(rc<0){if(errno==EINTR)return false;throw std::runtime_error("poll failed");}
    bool progress=false;
    for(size_t i=0;i<2;i++)if(fds[i].revents&POLLIN) {
      int fd=accept4(fds[i].fd,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);
      if(fd>=0) {
        ucred credential{};socklen_t size=sizeof(credential);
        if(peers.size()>=64||getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&credential,&size)||credential.uid!=geteuid())close(fd);
        else peers.emplace(fd,Peer{fd,i==1,{},{},nullptr,0});
        progress=true;
      }
    }
    for(size_t i=2;i<fds.size()&&budget&&!stopping;i++) {
      int fd=fds[i].fd;auto it=peers.find(fd);if(it==peers.end())continue;
      auto &p=it->second;bool dead=fds[i].revents&(POLLERR|POLLNVAL);
      if((fds[i].revents&(POLLIN|POLLHUP))&&p.in.find('\n')==std::string::npos) {
        char buffer[8192];auto n=recv(fd,buffer,sizeof(buffer),0);
        if(n>0){p.in.append(buffer,n);progress=true;}
        else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))dead=true;
      }
      if(p.in.size()>65536)dead=true;
      if(dead){disconnect(fd);progress=true;continue;}
      if(!p.control)next_app_fd=fd+1;
      size_t end;unsigned per_peer=4;
      while(budget&&per_peer--&&!stopping&&(end=p.in.find('\n'))!=std::string::npos) {
        auto line=p.in.substr(0,end);p.in.erase(0,end+1);request(p,line);--budget;progress=true;
        if(p.out.size()>1024*1024){dead=true;break;}
      }
      if(dead)disconnect(fd);
    }
    // Release copied slots before polling for more already-queued submissions.
    flush();return progress;
  }
  void present() {
        double start=now();bool shells=false;
        for(auto &[id,w]:engine->windows)shells|=w.visible&&w.shell_pending;
        if(!shells)apply_pending_content();
        engine->render(false,!scene_dirty);service_render_ms=(now()-start)*1000;scene_dirty=false;
        if(shells){if(engine->stats.published)++shell_publications;last_shell_render=engine->render_report();}
        feedback=generation;dirty=false;
        for(auto &[id,w]:engine->windows)if(w.visible&&w.shell_pending){
          w.shell_pending=false;
          if(w.has_content){w.splash_pending=false;dirty=true;scene_dirty=true;engine->ui_dirty=true;engine->ui_layout_dirty=true;engine->ui_mask_dirty=true;}
        }
        // Keep pending conversions alive when another window's shell was shown first.
        if(shells)for(auto &[id,r]:records)if(r.pending.commits){dirty=true;scene_dirty=true;}
        if(!dirty)presented=generation;
        for(auto &[fd,p]:peers)if(!p.wait_id.is_null()&&p.wait_generation<=(p.feedback_wait?feedback:presented)) {
          reply(p,p.wait_id,json_object("generation",p.feedback_wait?feedback:presented,"presented",!p.feedback_wait,"feedback",p.feedback_wait));p.wait_id=nullptr;
        }
        flush();
  }
  void run() {
    std::cout<<"TRIANGLE_BOAYO_READY "<<path<<std::endl;
    while(!stopping) {
      if(capture.owner>=0&&microseconds_now()-last_input_us>30000000){cancel_input();changed(false,false);}
      unsigned budget=32;
      for(unsigned pass=0;pass<4&&budget&&!stopping;pass++)if(!pump(dirty||pass!=0,budget))break;
      // No debounce sleep: drain a bounded ready backlog, then publish its latest state.
      if(dirty&&!stopping) {
        present();
      }
    }
    flush();
  }

};
} // namespace triangle_service

#ifndef TRIANGLE_SERVICE_ONLY
int main(int argc,char **argv) {
  try {
    signal(SIGPIPE,SIG_IGN); signal(SIGTERM,triangle_service::stop); signal(SIGINT,triangle_service::stop);
    if(argc==3 && std::string(argv[1])=="--ctl") {
      Client client("internal-control",socket_path()+".control"); auto q=J::parse(argv[2]);
      auto result=client.call(q.at("op"),q.value("args",J::object()));
      if(result.is_object() && result.contains("generation")) client.call("wait_present",{{"generation",result.at("generation")}});
      std::cout<<result.dump()<<std::endl; return 0;
    }
    triangle_service::Server server(argc==2 && std::string(argv[1])=="--mock"); server.run();
  } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
}

#endif
