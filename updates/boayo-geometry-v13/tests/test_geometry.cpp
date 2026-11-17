#include "geometry_math.hpp"
#include "culling.hpp"
#include <random>
#include <iostream>
using namespace boayo;
int main(){Geometry g;std::mt19937_64 rng(0x1300);uint64_t total=0,visible=0,old_candidates=0,new_candidates=0,scales=0;
 for(int e=-990;e<1000;++e)for(int d=-40;d<=40;++d){double x=std::ldexp(1.,e);for(int k=0;k<std::abs(d);++k)x=std::nextafter(x,d<0?0.:INFINITY);require(geometry_math::scale_fast(x)==geometry_math::scale_original(x),"near-power scale mismatch");++scales;}
 for(unsigned n=0;n<200000;++n){uint64_t bits=rng()&0x7fefffffffffffffull;double x;std::memcpy(&x,&bits,8);require(geometry_math::scale_fast(x)==geometry_math::scale_original(x),"random scale mismatch");++scales;}
 for(unsigned pose=0;pose<1200;++pose){double fh=pose<10?std::pow(10.,-double(pose)):0.01+(rng()%1599000)/10000.;double fv=pose<10?std::pow(10.,-double(9-pose)):0.01+(rng()%1599000)/10000.;
 Geometry::Projection p(1+rng()%2048,1+rng()%2048,double(int(rng()%72000)-36000)/100.,double(int(rng()%36000)-18000)/100.,double(int(rng()%36000)-18000)/100.,fh,fv);
 auto a=candidate_tiles(p,false),b=candidate_tiles(p,true);
 for(unsigned t=0;t<tiles;++t){auto ref=g.descriptor_reference(t,p);auto actual=g.descriptor(t,p);require(ref==actual,"descriptor math changed");require(!ref.second||b[t],"visible tile culled");total++;visible+=ref.second;old_candidates+=a[t];new_candidates+=b[t];}}
 std::cout<<J({{"result","PASS_EXACT_GEOMETRY"},{"tiles",total},{"visible",visible},{"v12_candidates",old_candidates},{"v13_candidates",new_candidates},{"scale_cases",scales}}).dump()<<std::endl;
 double cases[][7]={{640,440,-10,4,0,38,28},{40,100,-28.956508594668833,3.783709874320877,1.2984572681811244,2.88,10},{40,40,8.703651028610665,-7.350254556943742,2.48007595979405,4,4},{40,40,9.216035186353338,14.917598889100052,-5.127263251490149,4,4}};
 for(auto &c:cases){Geometry::Projection p(c[0],c[1],c[2],c[3],c[4],c[5],c[6]);auto a=candidate_tiles(p,false),b=candidate_tiles(p,true);unsigned old=0,fast=0;for(unsigned t=0;t<tiles;++t){old+=a[t];fast+=b[t];}std::cout<<"MOVE_CANDIDATES "<<old<<" -> "<<fast<<"\n";}
}
