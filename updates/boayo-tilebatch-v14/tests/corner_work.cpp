#include "geometry_math.hpp"
#include "mask.hpp"
#include <iostream>
using namespace boayo;
int main(){Geometry g;for(int radius:{14,0}){Geometry::Projection p(640,440,-10,4,0,38,28);MaskShape shape(J::array({{48,4,544,428,radius}}));unsigned full=0,partial=0,empty=0,visited=0;Bytes mask(1024);std::array<uint32_t,32> packed;
 for(unsigned t=0;t<tiles;++t){auto[d,v]=g.descriptor(t,p);if(!v)continue;bool whole;auto kind=prepare_mask(d,shape,mask,packed,whole);visited+=!whole;full+=kind==1;partial+=kind==2;empty+=!kind;}
 std::cout<<J({{"radius",radius},{"full",full},{"partial",partial},{"empty",empty},{"sampled_tiles",visited},{"integer_samples",shape.integer_samples},{"precise_samples",shape.precise_samples}}).dump()<<std::endl;}}
