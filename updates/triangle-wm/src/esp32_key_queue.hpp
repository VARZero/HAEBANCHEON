#pragma once
#include "key_transport.hpp"
// Transport-independent ESP32-side reference. The SPI slave task owns this queue;
// hand it key events through an RTOS queue when producers run on another task.
namespace key_transport {
struct RemoteQueue {
 struct Item {uint32_t sequence;uint8_t code,value;};
 std::array<Item,32> queue{};size_t start=0,count=0;uint32_t session,next=0;
 explicit RemoteQueue(uint32_t boot_session):session(boot_session?boot_session:1){}
 bool push(uint8_t code,uint8_t value){if(!code||code>6||value>2||count==queue.size())return false;if(!++next)++next;queue[(start+count++)%queue.size()]={next,code,value};return true;}
 Frame reply()const {Frame f{};memcpy(f.data(),"KBD1",4);put(f.data()+4,session);if(count){auto&i=queue[start];put(f.data()+8,i.sequence);f[12]=i.code;f[13]=i.value;}put(f.data()+16,crc(f.data(),16));return f;}
 bool consume(const Frame&ack){if(!count||memcmp(ack.data(),"KACK",4)||load(ack.data()+16)!=crc(ack.data(),16)||load(ack.data()+4)!=session||load(ack.data()+8)!=queue[start].sequence)return false;start=(start+1)%queue.size();--count;return true;}
};
}
