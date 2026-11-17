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
  uint64_t generation=0,solid_tiles=0,lazy_copied_tiles=0;
  std::array<uint64_t,tiles> wanted{};
  std::array<std::array<uint64_t,tiles>,2> stored{};
  // Exact ordered integer tuples; no hash collisions or per-tile JSON.
  std::array<std::array<std::vector<uint64_t>,tiles>,2> tile_keys;
  uint64_t pointer_version=0;
  uint64_t reused_tiles=0,packed_mask_tiles=0;
  uint64_t ready_frames=0;
  J metrics=J::object();
  ResidentCompositor(const Geometry &g,Projectors &p,Output &o):planner(g,p,true,true),output(o),dev(p.dev){}
  static bool supported(const WM &wm);
  void invalidate(){valid=false;}
  bool update(WM &wm);
  void execute(Layer &,const std::vector<unsigned> &,unsigned,bool,std::array<unsigned,2> &);
};
}
