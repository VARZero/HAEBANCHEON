#include "backend.hpp"
#include <chrono>
#include <iostream>
using namespace boayo;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
  volatile double sink = 0;
  double start = now();
  for (unsigned i = 0; i < 50; ++i) { Geometry g; sink += g.triangles[i][1][2]; }
  double geometry_ms = (now()-start)*1000/50;
  Geometry g;
  start = now();
  uint64_t checksum = 0;
  for (unsigned repeat = 0; repeat < 4; ++repeat) {
#ifdef BOAYO_V2
    Geometry::Projection p(640,360,37,-29,13,42,30);
#endif
    for (unsigned tile=0; tile<tiles; ++tile) {
#ifdef BOAYO_V2
      auto d=g.descriptor(tile,p);
#else
      auto d=g.descriptor(tile,640,360,37,-29,13,42,30);
#endif
      for (auto word : d.first) checksum += word;
    }
  }
  double descriptor_ms = (now()-start)*1000/4;
  WM wm; MockDevice dev; Projectors projectors(dev); Compositor comp(g,projectors); Output output(dev,g);
  auto scene=comp.update(wm,true);
#ifdef BOAYO_V2
  output.full(scene.data(),true);
#else
  output.full(scene.words);
#endif
  uint64_t initial_words=output.uploaded_words;
  Words patch(288,0); patch[0]=0x42505432;patch[1]=256;patch[2]=1;patch[3]=288;
  patch[18]=0;std::fill(patch.begin()+32,patch.end(),0x12345678);
  start=now();for(unsigned i=0;i<20;++i)output.patch(patch);
  double patch_ms=(now()-start)*1000/20;
  uint64_t hash=1469598103934665603ULL;
  for(auto word:output.shadow){hash^=word;hash*=1099511628211ULL;}
  std::cout << J({{"geometry_ms",geometry_ms},{"descriptor_batch_ms",descriptor_ms},
                 {"one_tile_patch_ms",patch_ms},{"initial_upload_bytes",initial_words*4},
                 {"descriptor_checksum",checksum},{"final_shadow_hash",hash},
                 {"scope","CPU emulator/host mock; not FPGA timings"}}).dump() << std::endl;
}
