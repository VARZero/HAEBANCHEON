#pragma once
namespace boayo {
// Start-to-start pacing. Work and status publication consume the period;
// an overrun starts the next dirty frame immediately, without a catch-up queue.
struct FramePacer {
  double last_start=0,interval=0;
  bool started=false;
  bool due(double now,double fps)const {return !started || now>=last_start+1./fps;}
  void begin(double now){interval=started?now-last_start:0;last_start=now;started=true;}
};
}
