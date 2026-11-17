#include "view_playback.hpp"
#include <iostream>
using namespace boayo;
int main(int argc,char**argv){
 Geometry g;MockDevice d;Output o(d,g);o.initialize_blank();WM w;w.view_yaw=8;w.view_pitch=4;w.view_roll=8;o.set_pose(8,4,8);
 auto original=o.camera;ViewPlayback p(o,w);p.start(argv[1],"test",0,5);
 for(const auto &f:p.frames)require(f.camera==g.camera(f.yaw/1000.,f.pitch/1000.,f.roll/1000.),"Python/C++ coefficient mismatch");
 p.tick(0,[]{return 0.;});require(p.submitted==1,"first frame");p.tick(.01,[]{return .01;});require(p.submitted==1,"no duplicate tick");
 p.tick(p.period*3+.001,[]{return .1;});require(p.submitted==2 && p.skipped==2,"late frame must skip backlog");
 p.stop();require(!p.active && o.camera==original && w.view_yaw==8 && w.view_pitch==4 && w.view_roll==8,"restore camera/WM");
 p.start(argv[1],"test",0,1);p.tick(1.1,[]{return 1.1;});require(!p.active && o.camera==original,"deadline restore");
 p.start(argv[1],"test",0,1);d.fail_after=0;bool failed=false;try{p.tick(.1,[]{return .1;});}catch(...){failed=true;}
 require(failed && !p.active && !p.error.empty() && o.camera==original && w.view_yaw==8,"failed device submission state");
 std::cout<<"PASS view coefficient, pacing, skip, restore, deadline, failure state\n";
}
