#include "key_transport.hpp"
#include "esp32_key_queue.hpp"
#include <cassert>
#include <iostream>
using namespace key_transport;
int main(){
 SerialParser parser;std::string bytes="\x1b[H\x1b[A\x1b[B\x1b[C\x1b[D\r\nKEY Home\nArrowLeft\n";std::vector<std::string> keys;
 for(unsigned char b:bytes){auto next=parser.feed(&b,1,1000);keys.insert(keys.end(),next.begin(),next.end());}
 assert((keys==std::vector<std::string>{"Home","ArrowUp","ArrowDown","ArrowRight","ArrowLeft","Enter","Home","ArrowLeft"}));
 assert(crc(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
 Frame f{};memcpy(f.data(),"KBD1",4);put(f.data()+4,12);put(f.data()+8,34);f[12]=6;f[13]=1;put(f.data()+16,crc(f.data(),16));Event e;assert(decode(f,e)&&e.session==12&&e.sequence==34&&e.code==6);
 for(unsigned i=0;i<20;i++){auto corrupt=f;corrupt[i]^=1;assert(!decode(corrupt,e));}
 auto ack=acknowledge(12,34);assert(!memcmp(ack.data(),"KACK",4)&&load(ack.data()+16)==crc(ack.data(),16));
 RemoteQueue queue(123);for(int i=0;i<32;i++)assert(queue.push(3,1));assert(!queue.push(6,1));auto head=queue.reply();assert(decode(head,e)&&e.sequence==1);assert(queue.reply()==head);assert(!queue.consume(acknowledge(124,1)));assert(queue.consume(acknowledge(123,1)));assert(!queue.consume(acknowledge(123,1)));assert(queue.count==31);
 std::cout<<"PASS_KEY_TRANSPORT split-ANSI CRLF CRC corruption SPI-session-sequence\n";
}
