#pragma once
#include "ipc.hpp"
#include "content.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <memory>
#include <fstream>
#include <chrono>

namespace app_ui {
// The first prototype uses the resident ASCII bitmap font. No runtime font I/O.
inline const char* letter(char c){
 static const char* lower[]={
 "00000E011F111F","10101E1111111E","00000F1010100F","01010F1111110F",
 "00000E111F100F","0608081E080808","00000F11110F01","10101E11111111",
 "04000C0404040E","0200060202120C","10101214181412","0C04040404040E",
 "00001A15151111","00001E11111111","00000E1111110E","00001E11111E10",
 "00000F11110F01","00001619101010","00000F100E011E","08081E08080906",
 "0000111111130D","00001111110A04","0000111115150A","0000110A040A11",
 "00001111110F01","00001F0204081F"};
 if(c>='a'&&c<='z')return lower[c-'a'];
 switch(c){
 case '!':return "04040404040004";case '?':return "0E110102040004";
 case ',':return "00000000000C08";case ';':return "00040400040408";
 case '+':return "0004041F040400";case '=':return "00001F001F0000";
 case '_':return "0000000000001F";case '(':return "02040808080402";
 case ')':return "08040202020408";case '[':return "0E08080808080E";
 case ']':return "0E02020202020E";case '<':return "01020408040201";
 case '>':return "10080402040810";case '"':return "0A0A0000000000";
 case '\'':return "04040000000000";case '#':return "0A1F0A0A1F0A00";
 case '%':return "19190204081313";case '*':return "00150E1F0E1500";
 case '\\':return "10100804020101";case '|':return "04040404040404";
 case '{':return "02040408040402";case '}':return "08040402040408";
 case '@':return "0E11171B17100F";case '$':return "040F140E051E04";
 case '&':return "0C12140C15120D";case '^':return "040A1100000000";
 case '`':return "08040000000000";case '~':return "00000916000000";
 default:return glyph(c);
 }
}
struct Canvas {
 J targets=J::array();
 void target(const std::string&id,int x,int y,int w,int h,bool enabled=true){targets.push_back(J{{"id",id},{"x",x},{"y",y},{"w",w},{"h",h},{"enabled",enabled}});}
 unsigned width=360,height=240;unsigned background=36;std::vector<uint8_t> pixels;
 Canvas(unsigned w=360,unsigned h=240):width(w),height(h),pixels(size_t(w)*h,36){}
 void clear(unsigned color){background=color;std::fill(pixels.begin(),pixels.end(),uint8_t(color));}
 void rect(int x,int y,int w,int h,unsigned color){
  auto r=intersect({x,y,w,h},{0,0,int(width),int(height)});
  for(int row=r.y;row<r.y+r.h;row++)std::fill_n(pixels.begin()+size_t(row)*width+r.x,r.w,uint8_t(color));
 }
 void text(int x,int y,const std::string&s,unsigned color=255,unsigned scale=1){
  int start=x;for(unsigned char c:s){if(c=='\n'){x=start;y+=9*scale;continue;}if(c=='\t'){x+=24*scale;continue;}
   const char*g=letter(char(c));for(int row=0;row<7;row++){unsigned bits=hex_digit(g[2*row])*16+hex_digit(g[2*row+1]);for(int col=0;col<5;col++)if(bits&(1u<<(4-col)))rect(x+col*scale,y+row*scale,scale,scale,color);}x+=6*scale;
  }
 }
 void button(int x,int y,int w,const std::string&label,bool selected=false){rect(x,y,w,25,selected?3:0);text(x+9,y+9,label,selected?0:255);}
 void ppm(const std::string&path)const{
  std::ofstream f(path,std::ios::binary);require(bool(f),"cannot open preview output");f<<"P6\n"<<width<<' '<<height<<"\n255\n";
  for(unsigned i:pixels){auto c=palette(i);char rgb[3]={char(c>>16),char(c>>8),char(c)};f.write(rgb,3);}require(bool(f),"preview write failed");
 }
};
inline bool inside(int x,int y,int rx,int ry,int w,int h){return x>=rx&&y>=ry&&x<rx+w&&y<ry+h;}
inline bool ascii(const std::string&s){for(unsigned char c:s)if(!((c>=32&&c<=126)||c=='\n'||c=='\t'))return false;return true;}
struct Window {
 J published_targets=J::array();std::string focused_target;
 Client client;int id=0,fd=-1;size_t slot_bytes=0;uint8_t*mapped=nullptr;uint64_t sequence=0;
 std::vector<uint8_t> displayed;unsigned width,height;uint64_t submitted_bytes=0;
 bool style_pending=false;unsigned pending_background=36,pending_splash=228;
 Window(const std::string&title,unsigned w,unsigned h,unsigned background=36,unsigned splash=228):client(title),width(w),height(h){
  auto created=client.call("create_window",{{"title",title},{"surface_width",w},{"surface_height",h},{"width_deg",28},{"height_deg",20},{"padding_deg",3},{"background_index",background},{"splash_index",splash}});
  id=created.at("window_id");client.call("wait_feedback",{{"generation",created.at("generation")}});
  auto info=client.call("create_surface",{{"window_id",id},{"format","RGB332"}});slot_bytes=info.at("slot_bytes");
  fd=open(info.at("path").get<std::string>().c_str(),O_RDWR|O_CLOEXEC);require(fd>=0,"open application surface");
  void*address=mmap(nullptr,slot_bytes*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
  if(address==MAP_FAILED){close(fd);fd=-1;throw std::runtime_error("map application surface");}mapped=static_cast<uint8_t*>(address);
 }
 ~Window(){if(mapped)munmap(mapped,slot_bytes*2);if(fd>=0)close(fd);}
 std::vector<DamageRect> damage(const Canvas&c)const{
  require(c.width==width&&c.height==height,"application frame size changed");
  if(displayed.empty())return {{0,0,int(width),int(height)}};
  std::vector<DamageRect> regions;DamageRect all{};
  for(unsigned y=0;y<height;y++){
   int first=0,last=width-1;size_t row=size_t(y)*width;
   while(first<int(width)&&c.pixels[row+first]==displayed[row+first])first++;
   if(first==int(width))continue;
   while(last>first&&c.pixels[row+last]==displayed[row+last])last--;
   DamageRect r{first,int(y),last-first+1,1};all=unite(all,r);
   if(!regions.empty()&&regions.back().x==r.x&&regions.back().w==r.w&&regions.back().y+regions.back().h==r.y)regions.back().h++;
   else regions.push_back(r);
  }
  if(regions.size()>128)return {all};return regions;
 }
 bool present(const Canvas&source){
  Canvas decorated=source;
  if(source.targets!=published_targets){auto state=client.call("set_focusables",{{"window_id",id},{"targets",source.targets}});published_targets=source.targets;if(state.at("window_id")==id)focused_target=state.at("target").get<std::string>();}
  for(auto&t:source.targets)if(t.at("id")==focused_target&&t.value("enabled",true)){int x=t.at("x"),y=t.at("y"),w=t.at("w"),h=t.at("h");for(int i=0;i<2&&w>2*i&&h>2*i;i++){unsigned color=i?255:0;decorated.rect(x+i,y+i,w-2*i,1,color);decorated.rect(x+i,y+h-1-i,w-2*i,1,color);decorated.rect(x+i,y+i,1,h-2*i,color);decorated.rect(x+w-1-i,y+i,1,h-2*i,color);}}
  const Canvas&c=decorated;
  auto changes=damage(c);if(changes.empty()){if(!style_pending)return false;changes.push_back({0,0,1,1});}unsigned slot=sequence%2;J regions=J::array();
  for(auto r:changes){for(int y=r.y;y<r.y+r.h;y++)std::memcpy(mapped+slot*slot_bytes+size_t(y)*width+r.x,c.pixels.data()+size_t(y)*width+r.x,r.w);regions.push_back({r.x,r.y,r.w,r.h});}
  J request={{"window_id",id},{"sequence",sequence+1},{"slot",slot},{"damage",regions}};
  if(style_pending){request["background_index"]=pending_background;request["splash_index"]=pending_splash;}
  auto reply=client.call("commit_surface",request);sequence++;displayed=c.pixels;style_pending=false;submitted_bytes+=reply.at("copied_bytes").get<uint64_t>();
  client.call("wait_present",{{"generation",reply.at("generation")}});return true;
 }
 J events(unsigned timeout_ms=1000){auto events=client.call("wait_events",{{"timeout_ms",timeout_ms}}).at("events");for(auto&e:events)if(e.at("window_id")==id&&e.value("type",std::string())=="focus")focused_target=e.at("target").get<std::string>();return events;}
 void colors(unsigned background,unsigned splash){require(background<256&&splash<256,"application color bounds");pending_background=background;pending_splash=splash;style_pending=true;}
};
}
