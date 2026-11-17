#pragma once
#include "apps.hpp"
namespace boayo {
// Opt-in exercise of the normal WM input and device-output paths.
// The caller advances only after rendered_generation reaches the previous token.
struct MotionExercise {
  WM &wm; Desktop &desktop;
  bool active=false;std::string owner,phase="idle";
  std::vector<int> ids;int next=0,saved_focus=0;
  double saved_yaw=0,saved_pitch=0,saved_roll=0;
  J launcher;bool launcher_mapped=false;
  std::set<std::string> saved_feedback;
  uint64_t token=0;
  static constexpr int steps=25;
  MotionExercise(WM &w,Desktop &d):wm(w),desktop(d){}
  J call(const std::string &op,J args=J::object()) {
    return wm.dispatch({{"op",op},{"app",owner},{"args",args}});
  }
  J state() const {return {{"active",active},{"phase",phase},{"next_step",next},
    {"steps",steps},{"target_generation",token},{"windows",ids},
    {"view",{{"yaw",wm.view_yaw},{"pitch",wm.view_pitch},{"roll",wm.view_roll}}}};}
  void pose(double y,double p,double r){call("view_set",{{"yaw",y},{"pitch",p},{"roll",r}});}
  J stop(double now) {
    if(!active)return state();
    if(wm.input_sources.count(owner) && wm.input_sources.at(owner).count("left"))
      wm.input_button(owner,"left",false);
    // Remove only windows created by this exercise; leave existing apps alive.
    for(int id:ids){auto it=desktop.running.find(id);if(it!=desktop.running.end()){
      it->second->close();wm.disconnect(it->second->owner);desktop.running.erase(it);}}
    for(auto it=desktop.workspace.feedback.begin();it!=desktop.workspace.feedback.end();){
      if(!saved_feedback.count(it->first)){
        wm.destroy(desktop.workspace.owner,it->second);
        desktop.workspace.feedback_keys.erase(it->first);it=desktop.workspace.feedback.erase(it);
      }else ++it;
    }
    active=false;phase="restoring";
    if(wm.windows.count(desktop.launcher)){
      desktop.workspace.call("configure_window",{{"window_id",desktop.launcher},
        {"mapped",launcher_mapped},{"azimuth",launcher["azimuth"]},{"elevation",launcher["elevation"]}});
      desktop.mapped=launcher_mapped;
    }
    if(saved_focus && wm.windows.count(saved_focus))wm.focus_window(saved_focus,false);
    pose(saved_yaw,saved_pitch,saved_roll);
    desktop.update(now);token=wm.generation;phase="restored";return state();
  }
  J start(const std::string &who,double now) {
    require(!active,"motion exercise already active");
    require(wm.buttons.empty() && !wm.capture,"release pointer buttons before motion exercise");
    require(wm.windows.size()<=48,"not enough free window slots for motion exercise");
    owner=who;ids.clear();next=0;saved_focus=wm.focus;
    saved_yaw=wm.view_yaw;saved_pitch=wm.view_pitch;saved_roll=wm.view_roll;
    launcher=wm.window(desktop.launcher).data;launcher_mapped=desktop.mapped;
    saved_feedback.clear();for(const auto &v:desktop.workspace.feedback)saved_feedback.insert(v.first);
    active=true;phase="creating";
    try{
      for(const char *kind:{"dashboard","telemetry","sdk-pulse"})ids.push_back(desktop.launch(kind));
      pose(0,0,0);desktop.update(now);token=wm.generation;phase="ready";return state();
    }catch(...){stop(now);throw;}
  }
  App &app(unsigned index){require(index<ids.size(),"motion window index");
    auto it=desktop.running.find(ids[index]);require(it!=desktop.running.end(),"motion window was closed externally");return *it->second;}
  void focus(unsigned index,double now){wm.focus_window(app(index).id);desktop.update(now);}
  void control(unsigned index,const std::string &name,double da,double de,double now){
    focus(index,now);auto &a=app(index);int caption=a.captions.at(name);
    auto d=wm.window(caption).data;double az=d["azimuth"],el=d["elevation"];
    call("input_warp",{{"azimuth",az},{"elevation",el}});
    call("input_button",{{"button","left"},{"pressed",true}});desktop.update(now);
    if(name!="close"){
      require(!a.drag.is_null(),"caption input did not begin drag");
      call("input_warp",{{"azimuth",az+da},{"elevation",el+de}});desktop.update(now);
    }
    call("input_button",{{"button","left"},{"pressed",false}});desktop.update(now);
    require(!wm.capture && wm.buttons.empty(),"caption input did not release capture");
    if(name=="close")require(!wm.windows.count(ids[index]),"close caption did not destroy window");
  }
  J step(const std::string &who,int expected,uint64_t rendered,double now){
    require(active && owner==who,"motion exercise ownership mismatch");
    require(expected==next && next<steps,"motion step out of order");
    require(rendered>=token,"previous motion scene has not completed");
    static const double poses[10][3]={{0,0,0},{-12,0,0},{12,0,0},{0,-8,0},{0,8,0},
      {0,0,-20},{0,0,20},{-8,6,-15},{8,-6,15},{0,0,0}};
    if(next<10){phase=next>=7&&next<=8?"view-mixed":"view-axis";pose(poses[next][0],poses[next][1],poses[next][2]);}
    else if(next>=10&&next<=12){phase="window-azimuth-elevation";
      control(0,"move",next==11?20:-10,next==11?-8:4,now);}
    else if(next==13||next==14){phase="window-width-height";control(0,"resize_right",next==13?6:-6,next==13?-4:4,now);}
    else if(next==15){phase="overlap";control(1,"move",4,2,now);}
    else if(next==16||next==17){phase="front-back";focus(next==16?0:1,now);}
    else if(next==18||next==19){phase="second-window-move";control(2,"move",next==18?-12:12,next==18?4:-4,now);}
    else if(next==20){phase="view-mixed";pose(6,4,15);}
    else if(next==21){phase="view-reset";pose(0,0,0);}
    else {phase="close-caption";control(24-next,"close",0,0,now);}
    desktop.update(now);token=wm.generation;++next;return state();
  }
};
}
