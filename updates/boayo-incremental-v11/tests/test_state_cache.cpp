#include "state_cache.hpp"
#include <cassert>
#include <iostream>
using namespace boayo;
int main(){
 WM wm;SceneStateCache cache;unsigned checked=0;J mirror;
 auto check=[&]{assert(cache.state(wm)==wm.state());cache.apply(mirror);assert(mirror==wm.state());++checked;};
 auto call=[&](std::string op,J args){auto result=wm.dispatch({{"op",op},{"args",args},{"app","cache-test"}});check();return result;};
 check();int a=call("create_window",{{"title","a"},{"decorated",false}})["window_id"];
 int b=call("create_window",{{"title","b"},{"azimuth",12},{"elevation",-7}})["window_id"];
 for(int i=0;i<120;++i){int id=i%2?a:b;
  switch(i%12){
   case 0:call("input_warp",{{"azimuth",i%60-30},{"elevation",i%20-10}});break;
   case 1:call("configure_window",{{"window_id",id},{"azimuth",i%50-25},{"elevation",i%30-15}});break;
   case 2:call("configure_window",{{"window_id",id},{"roll",i%60-30},{"width_deg",20+i%50},{"height_deg",20+i%40}});break;
   case 3:call("configure_window",{{"window_id",id},{"mapped",bool(i%24)}});break;
   case 4:call("configure_window",{{"window_id",id},{"input_enabled",bool(i%24)}});break;
   case 5:call("configure_window",{{"window_id",id},{"shape",J::array({J::array({0,0,160,100,12})})}});break;
   case 6:call("focus_window",{{"window_id",id}});break;
   case 7:wm.pointer_visible=!wm.pointer_visible;wm.capture=id;wm.drag=id;check();break;
   case 8:call("input_button",{{"button","left"},{"pressed",true}});break;
   case 9:call("input_button",{{"button","left"},{"pressed",false}});break;
   case 10:wm.cancel();check();break;
   case 11:call("fill",{{"window_id",id},{"rgb",{i,40,120}}});break;
  }
  auto computations=cache.recomputes;check();assert(cache.recomputes==computations);
 }
 auto before_windows=cache.window_updates,before_pointer=cache.pointer_updates;
 check();assert(cache.changed_windows.empty() && !cache.pointer_changed);
 assert(cache.window_updates==before_windows && cache.pointer_updates==before_pointer);
 // Pixel-only changes, focus, scroll and button metadata must remain fresh,
 // while identical hit geometry can reuse the previous geometric result.
 check();auto computations=cache.recomputes;
 ++wm.generation;check();assert(cache.changed_windows.empty() && !cache.pointer_changed);
 ++wm.scroll_serial;wm.scroll_delta=3;wm.pointer_visible=true;check();
 assert(cache.recomputes==computations);
 call("destroy_window",{{"window_id",a}});call("destroy_window",{{"window_id",b}});
 int c=call("create_window",{{"title","replacement"}})["window_id"];(void)c;
 std::cout<<"PASS state cache: "<<checked<<" exact snapshots, geometry invalidation and fresh input state\n";
}
