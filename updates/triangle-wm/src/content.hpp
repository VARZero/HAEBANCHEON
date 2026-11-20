#pragma once
#include "geometry.hpp"
using namespace boayo;
struct DamageRect {int x=0,y=0,w=0,h=0;};
static DamageRect intersect(DamageRect a,DamageRect b){int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
static DamageRect unite(DamageRect a,DamageRect b){if(!a.w||!a.h)return b;if(!b.w||!b.h)return a;int x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y};}
static uint32_t source_color(unsigned k){return ((k>>5)*255/7<<16)|(((k>>2)&7)*255/7<<8)|((k&3)*255/3);}
// A small built-in font makes the board demo independent of a filesystem font service.
static const char* glyph(char c){
 static const char* chars="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ- :./";
 static const char* rows[]={"0E11131519110E","040C040404040E","0E11010204081F","1E01010601111E","02060A121F0202","1F101E0101110E","0608101E11110E","1F010204080808","0E11110E11110E","0E11110F01020C","0E11111F111111","1E11111E11111E","0F10101010100F","1E11111111111E","1F10101E10101F","1F10101E101010","0F10101711110F","1111111F111111","0E04040404040E","0702020212120C","11121418141211","1010101010101F","111B1515111111","11191513111111","0E11111111110E","1E11111E101010","0E11111115120D","1E11111E141211","0F10100E01011E","1F040404040404","1111111111110E","11111111110A04","11111115151B11","11110A040A1111","11110A04040404","1F01020408101F","0000001F000000","00000000000000","00040400040400","00000000000C0C","01010204081010"};
 const char*p=std::strchr(chars,c);return rows[p?p-chars:37];
}
static unsigned hex_digit(char c){return c<='9'?c-'0':c-'A'+10;}
struct Component {int id;DamageRect box;unsigned color=255,scale=2;std::string text;bool is_text=false;};
static Component component(const J&q,unsigned width,unsigned height){
 Component c{};c.id=q.at("id");require(c.id>0,"positive component id");c.box.x=q.at("x");c.box.y=q.at("y");c.color=q.value("color",255u);require(c.color<256,"component palette");
 std::string type=q.value("type",std::string("rect"));require(type=="rect"||type=="text","component type");c.is_text=type=="text";
 if(c.is_text){c.text=q.at("text");c.scale=q.value("scale",2u);require(c.scale>=1&&c.scale<=8&&c.text.size()<=64,"text bounds");for(char ch:c.text)require(ch>=32&&ch<=90,"font supports uppercase ASCII");c.box.w=int(c.text.size()*6*c.scale);c.box.h=7*c.scale;}
 else {c.box.w=q.at("w");c.box.h=q.at("h");}
 require(c.box.x>=0&&c.box.y>=0&&c.box.w>0&&c.box.h>0&&uint64_t(c.box.x)+c.box.w<=width&&uint64_t(c.box.y)+c.box.h<=height,"component outside content area");return c;
}
template<class Pixel> struct BasicSurface {
 static Pixel color_value(unsigned c){return Pixel(sizeof(Pixel)==1?c:source_color(c));}
 unsigned width=240,height=144,bg=84;std::vector<Pixel> pixels;std::vector<Component> components;DamageRect pending{};uint64_t revision=0;
 void init(unsigned w,unsigned h,unsigned color){require(w>=32&&w<=1024&&h>=32&&h<=1024&&uint64_t(w)*h<=262144,"surface bounds");width=w;height=h;bg=color;pixels.assign(size_t(w)*h,color_value(bg));pending={0,0,int(w),int(h)};}
 void repaint(DamageRect damage){
  damage=intersect(damage,{0,0,int(width),int(height)});if(!damage.w||!damage.h)return;
  for(int y=damage.y;y<damage.y+damage.h;y++)std::fill_n(pixels.begin()+y*width+damage.x,damage.w,color_value(bg));
  for(const auto&c:components){DamageRect clip=intersect(damage,c.box);if(!clip.w||!clip.h)continue;Pixel color=color_value(c.color);
   if(!c.is_text){for(int y=clip.y;y<clip.y+clip.h;y++)std::fill_n(pixels.begin()+y*width+clip.x,clip.w,color);}
   else for(int y=clip.y;y<clip.y+clip.h;y++)for(int x=clip.x;x<clip.x+clip.w;x++){unsigned xx=(x-c.box.x)/c.scale,yy=(y-c.box.y)/c.scale;unsigned index=xx/6,col=xx%6;if(index<c.text.size()&&col<5){const char*g=glyph(c.text[index]);unsigned bits=hex_digit(g[yy*2])*16+hex_digit(g[yy*2+1]);if(bits&(1u<<(4-col)))pixels[y*width+x]=color;}}
  }
  pending=unite(pending,damage);revision++;
 }
 DamageRect put(const J&q){Component c=component(q,width,height);DamageRect d=c.box;auto it=std::find_if(components.begin(),components.end(),[&](const Component&v){return v.id==c.id;});if(it==components.end()){require(components.size()<128,"component limit");components.push_back(c);}else{if(c.box.x==it->box.x&&c.box.y==it->box.y&&c.box.w==it->box.w&&c.box.h==it->box.h&&c.color==it->color&&c.scale==it->scale&&c.text==it->text&&c.is_text==it->is_text)return {};d=unite(d,it->box);*it=c;}repaint(d);return d;}
 DamageRect erase(int id){auto it=std::find_if(components.begin(),components.end(),[&](const Component&v){return v.id==id;});require(it!=components.end(),"component missing");DamageRect d=it->box;components.erase(it);repaint(d);return d;}
};
using Surface=BasicSurface<uint32_t>;
using IndexedSurface=BasicSurface<uint8_t>;
static bool affected(const Descriptor&d,DamageRect r){
 int64_t planes[5][4];for(int k=0;k<4;k++){int64_t u=int32_t(d[6+k]),v=int32_t(d[10+k]),z=int32_t(d[14+k]);planes[0][k]=z;planes[1][k]=u-(r.x-1)*z;planes[2][k]=(r.x+r.w)*z-u;planes[3][k]=v-(r.y-1)*z;planes[4][k]=(r.y+r.h)*z-v;}
 for(int j=0;j<5;j++){auto*p=planes[j];int64_t upper=std::max({p[0],p[0]+31*p[1],p[0]+31*(p[1]+p[2])})+std::max(int64_t(0),p[3]);if(j==0?upper<=0:upper<0)return false;}return true;
}
