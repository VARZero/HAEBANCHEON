#pragma once
#include "backend.hpp"
namespace boayo {
// Production path: CPU owns metadata and source images, never a full scene.
struct ResidentCompositor {
  Compositor planner;
  Output &output;
  Device &dev;
  bool valid=false;
  uint64_t frames=0,total_jobs=0,cleared_tiles=0,blended_tiles=0;
  J metrics=J::object();
  ResidentCompositor(const Geometry &g,Projectors &p,Output &o):planner(g,p,true,true),output(o),dev(p.dev){}
  static bool supported(const WM &wm);
  void invalidate(){valid=false;}
  bool update(WM &wm);
  void execute(Layer &,const std::vector<unsigned> &,unsigned,bool,std::array<unsigned,2> &);
};
}
