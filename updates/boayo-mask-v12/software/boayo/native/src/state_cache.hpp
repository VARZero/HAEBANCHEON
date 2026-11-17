#pragma once
#include "wm.hpp"
namespace boayo {
inline void update_fields(J &to,const J &from){
  if(to==from)return;
  if(!to.is_object() || !from.is_object()){to=from;return;}
  for(auto i=to.begin();i!=to.end();)if(!from.contains(i.key()))i=to.erase(i);else ++i;
  for(auto i=from.begin();i!=from.end();++i)update_fields(to[i.key()],i.value());
}
struct SceneStateCache {
  struct Entry {J raw,visible;};
  std::map<int,Entry> entries;std::vector<int> order;
  J cached;Hit hit;
  uint64_t queries=0,recomputes=0,window_updates=0,pointer_updates=0;
  std::vector<size_t> changed_windows;
  bool reordered=false,pointer_changed=false;
  static bool same_geometry(const J&a,const J&b){
    if(!a.is_object())return false;
    for(const char *key:{"mapped","input_enabled","azimuth","elevation","roll","width_deg","height_deg","surface_width","surface_height","shape","decorated"})
      if(a.at(key)!=b.at(key))return false;
    return true;
  }
  const J &state(const WM &wm){
    ++queries;changed_windows.clear();reordered=order!=wm.order || cached.is_null();
    bool hit_changed=reordered || !cached.contains("pointer");
    if(!hit_changed)hit_changed=cached["pointer"]["azimuth"]!=wm.paz || cached["pointer"]["elevation"]!=wm.pel;
    for(auto i=entries.begin();i!=entries.end();)if(!wm.windows.count(i->first))i=entries.erase(i);else ++i;
    for(size_t index=0;index<wm.order.size();++index){int id=wm.order[index];auto &e=entries[id];const auto &data=wm.windows.at(id).data;
      bool changed=e.raw!=data;
      if(changed){hit_changed|=!same_geometry(e.raw,data);e.raw=data;e.visible=wm.public_window(id);}
      if(e.visible["focused"]!=(id==wm.focus)){e.visible["focused"]=id==wm.focus;changed=true;}
      if(e.visible["z"]!=index){e.visible["z"]=index;changed=true;}
      if(changed){++window_updates;changed_windows.push_back(index);}
    }
    if(reordered){cached["windows"]=J::array();for(int id:wm.order)cached["windows"].push_back(entries.at(id).visible);order=wm.order;}
    else for(auto index:changed_windows)cached["windows"][index]=entries.at(order[index]).visible;
    if(hit_changed){hit=wm.hit(wm.paz,wm.pel);++recomputes;}
    auto &pointer=cached["pointer"];
    pointer_changed=hit_changed || pointer.is_null() || pointer["visible"]!=wm.pointer_visible ||
      pointer["dragging"]!=bool(wm.drag) || pointer["buttons"]!=wm.buttons ||
      pointer["left_press_serial"]!=wm.press_serial || pointer["last_left_press"]!=wm.last_press ||
      pointer["left_release_serial"]!=wm.release_serial || pointer["last_left_release"]!=wm.last_release ||
      pointer["cancel_serial"]!=wm.cancel_serial || pointer["captured_window"]!=(wm.capture?J(wm.capture):J(nullptr)) ||
      pointer["scroll_serial"]!=wm.scroll_serial || pointer["scroll_delta"]!=wm.scroll_delta;
    if(pointer_changed){pointer=wm.pointer_state(&hit);++pointer_updates;}
    cached["generation"]=wm.generation;cached["compositor"]="cpp-native";
    cached["focused_window"]=wm.focus?J(wm.focus):J(nullptr);
    return cached;
  }
  // Called under the publication lock: do not copy unchanged windows/pointer.
  void apply(J &target)const{
    for(const char *key:{"generation","compositor","focused_window"})if(target[key]!=cached.at(key))target[key]=cached.at(key);
    if(reordered || !target.contains("windows"))target["windows"]=cached.at("windows");
    else for(auto index:changed_windows)target["windows"][index]=cached.at("windows")[index];
    if(pointer_changed || !target.contains("pointer"))target["pointer"]=cached.at("pointer");
  }
};
}
