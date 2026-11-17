#pragma once
#include "backend.hpp"
#include "wm.hpp"
#include <cstring>
namespace boayo {
// Camera-only preview: one device owner, no concurrent scene mutation.
struct ViewPlayback {
  struct Frame {int32_t yaw,pitch,roll;std::array<int32_t,180> camera;};
  static_assert(sizeof(Frame)==732,"view frame ABI");
  Output &output; WM &wm;
  std::vector<Frame> frames;
  std::array<int32_t,180> saved_camera{};
  double saved_y=0,saved_p=0,saved_r=0,start_time=0,end_time=0,period=0;
  uint64_t submitted=0,skipped=0,last_due=0;
  double submit_ms=0,max_submit_ms=0; bool active=false;
  std::string owner,error;
  ViewPlayback(Output&o,WM&w):output(o),wm(w){}
  J state() const {return {{"active",active},{"submitted",submitted},{"skipped",skipped},
    {"period_ms",period*1000},{"submit_ms",submit_ms},{"max_submit_ms",max_submit_ms},
    {"yaw",wm.view_yaw},{"pitch",wm.view_pitch},{"roll",wm.view_roll},{"error",error},
    {"mode","camera-only; window updates paused"}};}
  void start(const std::string &path,const std::string &who,double now,double duration) {
    require(!active,"view playback already active");
    require(duration>=1 && duration<=300 && std::isfinite(duration),"view duration 1..300 seconds");
    std::ifstream file(path,std::ios::binary);require(bool(file),"open view path");
    char magic[8];uint32_t h[3];file.read(magic,8);file.read(reinterpret_cast<char*>(h),12);
    require(file && std::memcmp(magic,"BOVIEW01",8)==0,"view path header");
    require(h[0]>=2 && h[0]<=3600 && h[1]>=16667 && h[1]<=1000000,"view path bounds");
    std::vector<Frame> loaded(h[0]);file.read(reinterpret_cast<char*>(loaded.data()),loaded.size()*sizeof(Frame));
    require(bool(file) && file.peek()==EOF,"view path size");
    uint32_t hash=2166136261u;for(size_t i=0;i<loaded.size()*sizeof(Frame);++i){hash^=reinterpret_cast<const uint8_t*>(loaded.data())[i];hash*=16777619u;}
    require(hash==h[2],"view path checksum");
    for(const auto &f:loaded)require(f.yaw>=-180000 && f.yaw<180000 && f.pitch>=-85000 && f.pitch<=85000 && f.roll>=-180000 && f.roll<180000,"view pose bounds");
    frames=std::move(loaded);period=h[1]/1000000.;owner=who;
    saved_camera=output.camera;saved_y=wm.view_yaw;saved_p=wm.view_pitch;saved_r=wm.view_roll;
    start_time=now;end_time=now+duration;submitted=skipped=last_due=0;submit_ms=max_submit_ms=0;error.clear();active=true;
  }
  void set_camera(const std::array<int32_t,180> &camera) {
    try{output.dev.pose(camera);output.camera=camera;}catch(...){output.failed=true;throw;}
  }
  void stop() {
    if(!active)return;
    active=false;
    set_camera(saved_camera);wm.view_yaw=saved_y;wm.view_pitch=saved_p;wm.view_roll=saved_r;
  }
  // Caller supplies a fresh monotonic time; no backlog of obsolete poses.
  template<class Clock> void tick(double now,Clock clock) {
    if(!active)return;
    try {
      if(now>=end_time){stop();return;}
      uint64_t due=uint64_t(std::max(0.,(now-start_time)/period));
      if(submitted && due<=last_due)return;
      const auto &f=frames[due%frames.size()];double before=clock();
      set_camera(f.camera);
      submit_ms=(clock()-before)*1000;max_submit_ms=std::max(max_submit_ms,submit_ms);
      if(submitted && due>last_due+1)skipped+=due-last_due-1;
      last_due=due;++submitted;wm.view_yaw=f.yaw/1000.;wm.view_pitch=f.pitch/1000.;wm.view_roll=f.roll/1000.;
    } catch(const std::exception &e){active=false;error=e.what();throw;}
  }
};
}
