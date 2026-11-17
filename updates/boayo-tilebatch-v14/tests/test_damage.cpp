#include "wm.hpp"
#include <random>
#include <iostream>
using namespace boayo;
int main(){
 WM wm;int id=wm.dispatch({{"op","create_window"},{"app","test"},{"args",{{"title","damage-test"},{"surface_width",512},{"surface_height",512}}}})["window_id"];
 auto &win=wm.window(id);win.dirty={};win.dirty_regions.clear();
 for(int n=0;n<32;++n)wm.dispatch({{"op","update_surface"},{"app","test"},{"args",{{"window_id",id},{"x",n*16},{"y",n*16},{"width",16},{"height",16},{"rgb24",base64(Bytes(16*16*3,97))}}}});
 unsigned area=0;for(auto r:win.dirty_regions)area+=r.w*r.h;
 require(area==32*16*16,"RPC updates lost precise damage");
 require(win.dirty_regions_revision==win.revision,"RPC dirty revision");
std::mt19937 r(913);uint64_t checks=0;for(unsigned trial=0;trial<3000;++trial){unsigned w=1+r()%257,h=1+r()%257;std::vector<Rect> input;std::vector<bool> needed(w*h),covered(w*h);for(unsigned n=r()%60;n;--n){Rect q{int(r()%(w+30))-15,int(r()%(h+30))-15,1+int(r()%40),1+int(r()%40)};input.push_back(q);q=intersect(q,{0,0,int(w),int(h)});for(int y=q.y;y<q.y+q.h;++y)for(int x=q.x;x<q.x+q.w;++x)needed[y*w+x]=true;}
 auto out=tiled_regions(input,w,h);for(auto q:out){require(q.x>=0&&q.y>=0&&q.w>0&&q.h>0&&q.x+q.w<=int(w)&&q.y+q.h<=int(h),"bounds");for(int y=q.y;y<q.y+q.h;++y)for(int x=q.x;x<q.x+q.w;++x)covered[y*w+x]=true;}
 for(unsigned i=0;i<w*h;++i){require(!needed[i]||covered[i],"lost dirty pixel");++checks;}
 }
 auto v=tiled_regions({{31,31,2,2}},64,64);unsigned crossing=0;for(auto a:v)crossing+=a.w*a.h;require(crossing==4,"tile-crossing edit inflated");std::cout<<"PASS_DAMAGE_3000_RANDOM_CASES cells="<<checks<<"\n";}
