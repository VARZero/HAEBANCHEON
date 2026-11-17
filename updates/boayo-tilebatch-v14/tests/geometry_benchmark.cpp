#include "geometry_math.hpp"
#include "culling.hpp"
#include "mask.hpp"
#include <chrono>
#include <iostream>
using namespace boayo;
using Clock=std::chrono::steady_clock;
static auto now(){return Clock::now();}
static long long us(Clock::time_point a,Clock::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();}
static const double cases[][7]={{640,440,-10,4,0,38,28},{40,100,-28.956508594668833,3.783709874320877,1.2984572681811244,2.88,10},{40,40,8.703651028610665,-7.350254556943742,2.48007595979405,4,4},{40,40,9.216035186353338,14.917598889100052,-5.127263251490149,4,4}};
static Words run(const Geometry&g,unsigned index,bool fast){
 auto c=cases[index];auto t0=now();Geometry::Projection p(c[0],c[1],c[2],c[3],c[4],c[5],c[6]);auto t1=now();
 auto flags=candidate_tiles(p,fast);std::vector<unsigned> ids;for(unsigned t=0;t<tiles;++t)if(flags[t])ids.push_back(t);auto t2=now();
 std::vector<geometry_math::Terms> terms(ids.size());std::vector<std::pair<Descriptor,bool>> output(ids.size());auto t3=now();
 for(unsigned i=0;i<ids.size();++i)geometry_math::project(g.triangles[ids[i]],p,terms[i]);auto t4=now();
 for(auto&t:terms)geometry_math::coefficients(t);auto t5=now();
 for(auto&t:terms)t.scale=fast?geometry_math::scale_fast(t.bound):geometry_math::scale_original(t.bound);auto t6=now();
 for(unsigned i=0;i<ids.size();++i)output[i]=geometry_math::finish(ids[i],p,terms[i]);auto t7=now();
 J shape=index==0?J::array({{48,4,544,428,14}}):J::array({{0,0,40,index==1?100:40,8}});MaskShape compiled(shape);Words signature;Bytes mask(1024);std::array<uint32_t,32> packed;unsigned visible=0;
 for(unsigned i=0;i<ids.size();++i){auto &[d,v]=output[i];if(!v)continue;++visible;bool whole;auto kind=prepare_mask(d,compiled,mask,packed,whole);signature.push_back(ids[i]);signature.insert(signature.end(),d.begin(),d.end());signature.push_back(kind);signature.insert(signature.end(),packed.begin(),packed.end());}auto t8=now();
 std::cout<<"GEOMETRY_STAGE "<<J({{"mode",fast?"v13":"v12"},{"case",index},{"candidates",ids.size()},{"visible",visible},{"prepare_us",us(t0,t1)},{"cull_us",us(t1,t2)},{"scratch_us",us(t2,t3)},{"project_us",us(t3,t4)},{"coefficients_us",us(t4,t5)},{"scale_us",us(t5,t6)},{"encode_visibility_us",us(t6,t7)},{"masks_signature_us",us(t7,t8)},{"total_us",us(t0,t8)}}).dump()<<std::endl;
 return signature;
}
int main(){Geometry g;std::array<Words,4> old;for(unsigned i=0;i<4;++i)old[i]=run(g,i,false);for(unsigned i=0;i<4;++i)require(run(g,i,true)==old[i],"RV32 geometry/mask output mismatch");std::cout<<"PASS_RV32_GEOMETRY_STAGES_EXACT"<<std::endl;}
