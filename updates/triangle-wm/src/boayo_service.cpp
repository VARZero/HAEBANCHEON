// BoAYO's application protocol, with an independent triangle-tile WM.
#define TRIANGLE_ENGINE_ONLY
#include "triangle_wm.cpp"
#include "ipc.hpp"
#include "application_buffer.hpp"
#include "pending_content.hpp"
#include "fast_motion.hpp"
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <climits>
#include <spawn.h>
#include <sys/wait.h>

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
  J wait_id; uint64_t wait_generation=0;bool feedback_wait=false,input_wait=false;
  bool checkpoint_blocked=false;J deferred_request;
  uint32_t fast_last_sequence=0;bool fast_invalid=false;
  bool event_wait=false;uint64_t event_deadline_us=0;
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
#include "move_commit.inc"
#include "app_input.inc"
#include "ui_input.inc"
#include "keyboard_navigation.inc"
#include "input_mailbox.inc"

  explicit Server(bool mock):path(socket_path()) {
    lock.fd=open((path+".lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    require(lock.fd>=0 && flock(lock.fd,LOCK_EX|LOCK_NB)==0,"WM already running or lock unavailable");
    engine=std::make_unique<Engine>(mock);
    engine->ui_enabled=!(getenv("TRIANGLE_INPUT")&&std::string(getenv("TRIANGLE_INPUT"))=="0");
    engine->input_checkpoint=[this](bool apply){input_checkpoint(apply);};
    engine->reserve_window_colors();
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
      "padding_deg",coordinate(w.padding),"background_index",w.color,"splash_index",w.splash_color,"phase",w.splash_pending?"splash":(w.shell_pending||!w.plan_ready)?"background":"content","mapped",w.visible,"decorated",false,"controls_enabled",w.controls_enabled,"generation",generation);
  }
  void erase(int id) {
    focus_targets.erase(id);if(nav_window==id){nav_window=0;nav_target.clear();engine->keyboard_close_window=0;engine->ui_mask_dirty=true;}
    if(pending_transform){pending_transform.reset();transform_phase="failed";transform_error="scene changed before transform publication";}
    if(transform_rollback)rollback_transform("scene changed before transform publication");
    if(capture.id==id||engine->move_marker.active)cancel_input();
    const auto &r=records.at(id); reserved_bytes-=size_t(r.width)*r.height*(r.shared?4+3*r.shared->pixel_bytes:13);
    auto&events=app_input[r.owner];events.erase(std::remove_if(events.begin(),events.end(),[&](const J&e){return e.at("window_id")==id;}),events.end());
    if(events.size()<app_event_limit)events.push_back(json_object("type","closed","window_id",id));
    records.erase(id); engine->windows.erase(id);
    auto &order=engine->order; order.erase(std::remove(order.begin(),order.end(),id),order.end());
    engine->texture_cache.erase(id);
    if(focused==id) focused=order.empty()?0:order.back();
    changed();complete_app_waits();
  }
  void disconnect(int fd) {
    if(pending_input.owner==fd)apply_pending_input();
    if(capture.owner==fd){cancel_input();changed();}
    std::vector<int> gone; for(const auto &[id,r]:records) if(r.owner==fd) gone.push_back(id);
    for(int id:gone) erase(id);
    close(fd); peers.erase(fd);
    app_input.erase(fd);
  }
  void reply(Peer &p,const J &id,const J &result) {
    p.out+=wire::encode(J{{"id",id},{"ok",true},{"result",result}});
  }
  J dispatch(Peer &p,const std::string &op,const J &a) {
    require(a.is_object(),"args must be an object");
    if((transform_rollback||pending_transform)&&op!="input_submit"&&op!="input_progress"&&op!="input_status"&&op!="get_state"&&op!="app_events"&&op!="poll_events")present();
    if(op=="input_button"&&a.value("pressed",false)&&!engine->move_marker.active&&engine->ui_layout_dirty&&dirty)present();
    if(op=="input_submit")return submit_input(p,a);
    if(op=="input_progress"){require(p.control,"input requires control socket");return input_progress();}
    if(op.rfind("input_",0)==0&&op!="input_status")apply_pending_input();
    if(op.rfind("input_",0)==0)return input(p,op,a);
    if(op=="set_focusables")return register_focusables(p,a);
    if(op=="navigation_status"){require(p.control,"control connection required");return navigation_status();}
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
      w.splash_color=integer(a,"splash_index",228,0,255);w.shell_pending=true;w.splash_pending=true;
      require(a.value("roll",0.)==0.,"window roll is not supported");
      Engine::validate(w);auto phase=microseconds_now();w.footprint=engine->footprint(w);request_profile.footprint_us=microseconds_now()-phase;
      std::string title=a.value("title",std::string("Application")); require(title.size()<=256,"title too long");
      w.controls_enabled=title!="BoAYO launcher";
      if(engine->move_marker.active)cancel_input();
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
    if(op=="app_events" || op=="poll_events"){
      require(!p.control,"application events require an application connection");
      auto result=drain_app_events(p.fd);result["view"]={yaw,pitch,0};result["apps"]=J::array();result["stored"]=J::array();return result;
    }
    if(op=="sync_scene") return generation;
    if(op=="view_set") {
      require(p.control,"view_set requires the internal control connection");
      double y=a.value("yaw",0.), pi=a.value("pitch",0.);
      require(a.value("roll",0.)==0.,"view roll is not supported");
      require(std::isfinite(y)&&std::abs(y)<=180&&std::isfinite(pi)&&std::abs(pi)<=85,"view bounds");
      if(capture.owner>=0){cancel_input();changed(false,false);}
      apply_pending_content();
      if(engine->active<0)present();
      auto result=engine->operate({{"op","pose"},{"yaw",y},{"pitch",pi}});
      yaw=y; pitch=pi; result["yaw"]=yaw; result["pitch"]=pitch; result["roll"]=0; return result;
    }
    if(op=="pose_sweep") {
      require(p.control && engine->active>=0,"pose_sweep requires internal control and a scene");
      unsigned count=a.value("samples",61u);double amplitude=a.value("amplitude",8.);
      require(count>=3&&count<=361&&std::isfinite(amplitude)&&amplitude>=0&&amplitude<=30,"pose sweep bounds");
      if(capture.owner>=0){cancel_input();changed(false,false);}
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
      unsigned background=integer(a,"background_index",w.color,0,255),splash=integer(a,"splash_index",w.splash_color,0,255);
      bool style_changed=background!=w.color||splash!=w.splash_color;
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
      bool did_change=!w.has_content || total.w || style_changed;
      if(did_change) {
        bool first_content=!w.has_content;
        if(background!=w.color){w.color=background;w.plan_ready=false;}w.splash_color=splash;
        if(!w.shell_pending)w.splash_pending=false;
        if(!w.has_content) { total={0,0,int(r.width),int(r.height)}; w.plan_ready=false; }
        w.has_content=true; s.pending=unite(s.pending,total);
        r.pending.merge(areas);
        ++r.pending.commits;
        changed(first_content||style_changed);
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
      unsigned background=integer(a,"background_index",w.color,0,255),splash=integer(a,"splash_index",w.splash_color,0,255);
      bool color_changed=background!=w.color||splash!=w.splash_color;
      bool geometry=bounds.yaw!=w.yaw || bounds.pitch!=w.pitch || bounds.width!=w.width || bounds.height!=w.height || bounds.padding!=w.padding;
      if((capture.id==id||engine->move_marker.active)&&(geometry||color_changed||visible!=w.visible||title!=r.title))cancel_input();
      if(geometry || color_changed || visible!=w.visible || title!=r.title) {
        if(geometry) { w.yaw=bounds.yaw; w.pitch=bounds.pitch; w.width=bounds.width; w.height=bounds.height; w.padding=bounds.padding;
          auto phase=microseconds_now();w.footprint=engine->footprint(w);request_profile.footprint_us=microseconds_now()-phase;w.plan_ready=false; }
        if(background!=w.color){w.color=background;w.plan_ready=false;}
        w.splash_color=splash;w.visible=visible;w.controls_enabled=title!="BoAYO launcher"; r.title=title; changed();
      }
      return description(id);
    }
    if(op=="focus_window" || op=="raise_window") {
      if(engine->order.back()!=id) { if(engine->move_marker.active)cancel_input();auto &order=engine->order; order.erase(std::find(order.begin(),order.end(),id)); order.push_back(id); changed(); }
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
  void request(Peer &p,const std::string &line,J q=J()) {
    if(!wire::json_mode()&&fast_motion::recognizable(line)){accept_fast_motion(p,line);return;}
    J id=nullptr;auto started=microseconds_now();request_profile={};
    try {
      if(q.is_null())q=wire::decode(line); id=q.at("id"); require(id.is_number_integer(),"integer request id required");
      require(p.wait_id.is_null(),"wait_present already pending");
      const auto &op=q.at("op").get_ref<const std::string&>();static const J empty=J::object();const J &a=q.contains("args")?q.at("args"):empty;
      request_profile.parse_us=microseconds_now()-started;
      if(op=="wait_events") {
        require(!p.control,"application events require an application connection");
        int timeout=integer(a,"timeout_ms",1000,1,1000);
        if(!app_input[p.fd].empty())reply(p,id,drain_app_events(p.fd));
        else{p.wait_id=id;p.event_wait=true;p.event_deadline_us=microseconds_now()+uint64_t(timeout)*1000;}
      } else if(op=="wait_input") {
        require(p.control,"input requires control socket");
        require(a.at("ticket").is_number_integer(),"integer input ticket required");
        int64_t ticket=a.at("ticket");require(ticket>=0&&uint64_t(ticket)<=input_accepted,"unknown future input ticket");
        if(uint64_t(ticket)>input_visible){p.wait_id=id;p.wait_generation=ticket;p.input_wait=true;}
        else reply(p,id,input_progress());
      } else if(op=="wait_present"||op=="wait_feedback") {
        require(a.at("generation").is_number_integer(),"integer generation required");
        int64_t gen=a.at("generation"); require(gen>=0 && uint64_t(gen)<=generation,"unknown future generation");
        bool early=op=="wait_feedback",preview=p.control&&engine->move_marker.active&&!engine->move_marker.committing;uint64_t ready=(early||preview)?feedback:presented;
        if(uint64_t(gen)>ready) { p.wait_id=id; p.wait_generation=gen;p.feedback_wait=early;p.input_wait=false; }
        else reply(p,id,presentation_result(ready,early,preview));
      } else {
        auto phase=microseconds_now();auto result=dispatch(p,op,a);request_profile.dispatch_us=microseconds_now()-phase;phase=microseconds_now();
        reply(p,id,result);request_profile.reply_us=microseconds_now()-phase;
        if(op=="commit_surface"||op=="configure_window"||op=="create_window"||op=="destroy_window"||op=="raise_window"){
          request_profile.op=op;last_mutation=std::move(request_profile);
        }
      }
    } catch(const std::exception &e) { p.out+=wire::encode(J{{"id",id},{"ok",false},{"error",e.what()}}); }
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
      buffered|=wire::ready(p.in);
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
      auto &p=it->second;bool dead=p.fast_invalid||(fds[i].revents&(POLLERR|POLLNVAL));
      if((fds[i].revents&(POLLIN|POLLHUP))&&!wire::ready(p.in)) {
        char buffer[8192];auto n=recv(fd,buffer,sizeof(buffer),0);
        if(n>0){p.in.append(buffer,n);progress=true;}
        else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))dead=true;
      }
      if(p.in.size()>65536||wire::invalid(p.in))dead=true;
      if(dead){disconnect(fd);progress=true;continue;}
      if(!p.control)next_app_fd=fd+1;
      unsigned per_peer=4;
      while(budget&&per_peer--&&!stopping&&wire::ready(p.in)) {
        auto line=wire::take(p.in);J cached=std::move(p.deferred_request);p.deferred_request=nullptr;p.checkpoint_blocked=false;
        request(p,line,std::move(cached));--budget;progress=true;
        if(p.fast_invalid){dead=true;break;}
        if(p.out.size()>1024*1024){dead=true;break;}
      }
      if(dead)disconnect(fd);
    }
    // Release copied slots before polling for more already-queued submissions.
    flush();return progress;
  }
  void present() {
        double start=now();bool shells=false;
        if(engine->move_marker.active&&!engine->move_marker.committing){
          if(!engine->move_marker.frozen)engine->render(false,true);
          else if(engine->move_marker.needs_publish)engine->render_marker();
          engine->ui_dirty=false;
          service_render_ms=(now()-start)*1000;complete_input();feedback=generation;dirty=false;
          for(auto&[fd,p]:peers)if((p.control||p.feedback_wait)&&!p.event_wait&&!p.input_wait&&!p.wait_id.is_null()&&p.wait_generation<=feedback){reply(p,p.wait_id,presentation_result(feedback,p.feedback_wait,true));p.wait_id=nullptr;p.feedback_wait=false;}
          flush();return;
        }
        for(auto &[id,w]:engine->windows)shells|=w.visible&&w.shell_pending;
        // One old-content overlay publication may precede a pending content
        // batch. Never acknowledge application generations for this publication.
        if(input_applied>input_visible&&scene_dirty&&!shells&&!engine->ui_layout_dirty&&engine->active>=0&&!input_priority_used){
          input_priority_used=true;++input_priority_publications;
          engine->render(false,true);service_render_ms=(now()-start)*1000;
          complete_input();flush();return;
        }
        if(!shells)apply_pending_content();
        prepare_transform();
        try{
          if(transform_rollback&&engine->mock&&getenv("TRIANGLE_MOCK_FAIL_TRANSFORM"))throw std::runtime_error("injected transform preparation failure");
          engine->render(false,!scene_dirty);finish_transform();
        }catch(const std::exception&e){if(!transform_rollback)throw;rollback_transform(e.what());if(engine->failed)throw;return;}
        service_render_ms=(now()-start)*1000;scene_dirty=false;
        input_priority_used=false;complete_input();
        if(shells){if(engine->stats.published)++shell_publications;last_shell_render=engine->render_report();}
        feedback=generation;dirty=false;
        for(auto &[id,w]:engine->windows)if(w.visible&&w.shell_pending){
          w.shell_pending=false;
          if(w.has_content){w.splash_pending=false;dirty=true;scene_dirty=true;engine->ui_dirty=true;engine->ui_layout_dirty=true;engine->ui_mask_dirty=true;}
        }
        // Keep pending conversions alive when another window's shell was shown first.
        if(shells)for(auto &[id,r]:records)if(r.pending.commits){dirty=true;scene_dirty=true;}
        if(!dirty)presented=generation;
        for(auto &[fd,p]:peers)if(!p.wait_id.is_null()&&!p.event_wait&&!p.input_wait&&p.wait_generation<=(p.feedback_wait?feedback:presented)) {
          reply(p,p.wait_id,presentation_result(p.feedback_wait?feedback:presented,p.feedback_wait));p.wait_id=nullptr;
        }
        flush();
  }
  void run() {
    std::cout<<"TRIANGLE_BOAYO_READY "<<path<<std::endl;
    while(!stopping) {
      reap_home();
      if(capture.owner>=0&&microseconds_now()-last_input_us>30000000){cancel_input();changed(false,false);}
      unsigned budget=32;
      for(unsigned pass=0;pass<4&&budget&&!stopping;pass++)if(!pump(dirty||pending_input.owner>=0||pass!=0,budget))break;
      apply_pending_input();
      // No debounce sleep: drain a bounded ready backlog, then publish its latest state.
      if(dirty&&!stopping) {
        present();
      }
      complete_app_waits();flush();
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
    triangle_service::Server server(argc==2 && std::string(argv[1])=="--mock"); server.run();return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl; return 1; }
}

#endif
