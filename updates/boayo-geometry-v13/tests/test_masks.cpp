#include "mask.hpp"
#include "wm.hpp"
#include <random>
#include <iostream>
using namespace boayo;
int main(){
 std::mt19937_64 rng(0x1273);uint64_t samples=0,fast_tiles=0,precise=0,integers=0,tiles_tested=0;
 auto check=[&](const Descriptor&d,const J&s){
  MaskShape shape(s);Bytes a(1024,0xa5);std::array<uint32_t,32> packed;packed.fill(0xa5a5a5a5);
  bool whole=false;auto kind=prepare_mask(d,shape,a,packed,whole);bool any=false,all=true;
  for(unsigned cell=0;cell<1024;++cell){auto q=coordinates(d,cell);
   bool expected=covered(d,q) && shape_contains(s,double(q[0])/q[2],double(q[1])/q[2]);
   require(a[cell]==expected && bool((packed[cell/32]>>(cell%32))&1)==expected,"mask pixel mismatch");
   any|=expected;all&=expected;++samples;
  }
  require(kind==(!any?0:all?1:2),"mask kind mismatch");
  fast_tiles+=whole;precise+=shape.precise_samples;integers+=shape.integer_samples;++tiles_tested;
 };
 Geometry g;
 std::vector<J> shapes={J::array(),J::array({{48,4,544,428,14}}),J::array({{0,0,640,440,0}}),
  J::array({{0,0,80,80,40},{400,220,100,100,25}}),J::array({{48.25,4.5,543.5,427.25,13.75}}),
  J::array({{0,0,1,440,0},{639,0,1,440,0}})};
 for(int pose=0;pose<24;++pose){
  Geometry::Projection p(640,440,double(int(rng()%360)-180),double(int(rng()%130)-65),double(int(rng()%80)-40),10+rng()%110,8+rng()%80);
  for(unsigned t=0;t<tiles;++t){auto [d,visible]=g.descriptor(t,p);if(visible)check(d,shapes[pose%shapes.size()]);}
 }
 // Descriptor extremes, including sign changes, protect the incremental lattice.
 for(int n=0;n<96;++n){Descriptor d{};d[2]=640;d[3]=440;
  for(int j=6;j<18;++j)d[j]=uint32_t(rng());
  check(d,shapes[n%shapes.size()]);
 }
 for(const auto&s:shapes){MaskShape c(s);
  for(unsigned n=0;n<100000;++n){int64_t z=int64_t(1)<<(rng()%38);
   int64_t x=int64_t(rng()%641)*z+int(rng()%3)-1,y=int64_t(rng()%441)*z+int(rng()%3)-1;
   std::array<int64_t,3> q{x,y,z};
   require(c.contains(q)==shape_contains(s,double(x)/z,double(y)/z),"homogeneous edge mismatch");
  }
 }
 require(fast_tiles>0 && integers>0 && precise>0,"all paths must be exercised");
 std::cout<<J({{"result","PASS_EXACT_MASKS"},{"tiles",tiles_tested},{"pixels",samples},{"edge_samples",600000},{"whole_tiles",fast_tiles},{"integer_samples",integers},{"precise_samples",precise}}).dump()<<std::endl;
}
