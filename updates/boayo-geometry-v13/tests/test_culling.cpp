#include "culling.hpp"
#include <random>
#include <iostream>
using namespace boayo;
int main(){Geometry g;std::mt19937_64 rng(0xc011);uint64_t skipped=0,visible=0,total=0;
 for(unsigned t=0;t<tiles;++t)for(auto &v:g.triangles[t])for(unsigned k=0;k<3;++k)
  require(v[k]>=tile_group_bounds[t/32][k] && v[k]<=tile_group_bounds[t/32][k+3],"stale fixed bounds");
 for(unsigned i=0;i<1200;++i){unsigned w=1+rng()%2048,h=1+rng()%2048;
 double fh=i<10?std::pow(10.,-double(i)):0.01+(rng()%1599000)/10000.;
 double fv=i<10?std::pow(10.,-double(9-i)):0.01+(rng()%1599000)/10000.;
 Geometry::Projection p(w,h,double(int(rng()%72000)-36000)/100.,double(int(rng()%36000)-18000)/100.,double(int(rng()%36000)-18000)/100.,fh,fv);
 auto keep=candidate_groups(p);
 for(unsigned t=0;t<tiles;++t){auto [d,v]=g.descriptor(t,p);require(!v || keep[t/32],"visible descriptor culled");visible+=v;skipped+=!keep[t/32];++total;}
 }
 std::cout<<"PASS_CONSERVATIVE_CULL tiles="<<total<<" visible="<<visible<<" skipped="<<skipped<<"\n";
}
