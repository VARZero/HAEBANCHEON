#include "binary_wire.hpp"
#include <cassert>
#include <iostream>
using namespace boayo;
int main(){
 J q={{"id",1},{"op","input_move"},{"args",{{"x",640},{"y",360},{"angle",-1.25}}}};
 std::string frame=wire::encode(q),buffer;
 assert(frame.substr(0,4)=="BWM1");
 for(size_t i=0;i<frame.size();i++){assert(!wire::ready(buffer));buffer+=frame[i];}
 assert(wire::ready(buffer));assert(wire::decode(wire::take(buffer))==q&&buffer.empty());
 buffer=frame+frame;assert(wire::decode(wire::take(buffer))==q);assert(wire::decode(wire::take(buffer))==q);
 auto payload=J::from_cbor(frame.begin()+8,frame.end());
 assert(payload["args"]["angle"].is_object()&&payload["args"]["angle"].at(wire::fixed_key())==-20971520);
 auto angle=wire::encode(J{{"yaw",1.125},{"pitch",-0.375},{"azimuth",360}});
 auto converted=wire::decode(angle.substr(8));assert(converted["yaw"]==1.25&&converted["pitch"]==-0.5&&converted["azimuth"]==360);
 auto encoded=J::from_cbor(angle.begin()+8,angle.end());assert(encoded["yaw"][wire::angle_key()]==5);
 buffer=frame;buffer[0]='X';assert(wire::invalid(buffer));
 buffer="BWM1";wire::append(buffer,wire::maximum+1);assert(wire::invalid(buffer));
 bool rejected=false;try{wire::encode(J{{wire::fixed_key(),1}});}catch(...){rejected=true;}assert(rejected);
 auto raw=J::to_cbor(J(0.5));rejected=false;try{wire::decode(std::string(raw.begin(),raw.end()));}catch(...){rejected=true;}assert(rejected);
 J deep=1;for(int i=0;i<34;i++)deep=J::array({deep});rejected=false;try{wire::encode(deep);}catch(...){rejected=true;}assert(rejected);
 // In-place traversal must preserve the exact old wire representation.
 for(int i=0;i<128;i++){
  J original={{"id",i},{"args",{{"yaw",(i-64)*0.125},{"items",J::array({i,true,nullptr,"text",(i-64)*0.03125})}}}};
  J transformed=original;wire::pack_inplace(transformed);assert(transformed==wire::pack(original));
  auto expected=wire::unpack(transformed);wire::unpack_inplace(transformed);assert(transformed==expected);
  assert(wire::encode(original)==wire::encode(J(original)));
 }
 std::cout<<"PASS_BINARY: fragmented/coalesced frames, fixed integer wire values, malformed input, reserved tags, nesting; move frame bytes="<<frame.size()<<"\n";
}
