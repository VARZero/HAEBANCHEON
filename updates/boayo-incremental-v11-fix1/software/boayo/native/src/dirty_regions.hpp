#pragma once
#include "texture.hpp"
namespace boayo {
struct Rect {int x=0,y=0,w=0,h=0;};
inline Rect intersect(Rect a,Rect b){int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
inline Rect bounds(Rect a,Rect b){if(!a.w || !a.h)return b;if(!b.w || !b.h)return a;int x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y};}
inline void add_rect(std::vector<Rect>&rs,Rect r){
 if(r.w<=0||r.h<=0)return;
 // Coalesce overlap/touch without joining distant widgets. Bound metadata growth.
 for(size_t i=0;i<rs.size();) {auto a=rs[i];
   bool join=r.x<=a.x+a.w && a.x<=r.x+r.w && r.y<=a.y+a.h && a.y<=r.y+r.h;
   if(join){r=bounds(r,a);rs.erase(rs.begin()+i);i=0;}else ++i;
 }
 rs.push_back(r);if(rs.size()>128){Rect all{};for(auto a:rs)all=bounds(all,a);rs={all};}
}
inline const Texture* texture_base(const Texture&t){return t.base?t.base.get():&t;}
inline bool same_patch(const Texture::Patch&a,const Texture::Patch&b){return a.source==b.source && a.src==b.src && a.dst==b.dst && a.width==b.width && a.height==b.height && a.src_stride==b.src_stride && a.dst_stride==b.dst_stride;}
inline Rect patch_rect(const Texture::Patch&p,unsigned width){return {int(p.dst%width),int(p.dst/width),int(p.width),int(p.height)};}
inline std::vector<Rect> texture_delta(const Texture*old,const Texture&next,unsigned w,unsigned h){
 if(!old || old->size()!=next.size() || texture_base(*old)!=texture_base(next))return {{0,0,int(w),int(h)}};
 if(old==&next)return {};
 std::vector<Rect> dirty;
 for(size_t i=0;i<std::max(old->patches.size(),next.patches.size());++i){
  auto a=i<old->patches.size()?&old->patches[i]:nullptr;auto b=i<next.patches.size()?&next.patches[i]:nullptr;
  if(a && b && same_patch(*a,*b))continue;
  for(auto p:{a,b})if(p){if(p->dst_stride!=w || p->dst%w+p->width>w)return {{0,0,int(w),int(h)}};add_rect(dirty,patch_rect(*p,w));}
 }
 return dirty;
}
// Conservative descriptor/rectangle intersection, including sampling margin.
inline bool rect_affects(const std::array<uint32_t,18>&d,Rect r){
 std::array<std::array<int64_t,4>,5> p;
 for(int k=0;k<4;k++){int64_t u=int32_t(d[6+k]),v=int32_t(d[10+k]),z=int32_t(d[14+k]);p[0][k]=z;p[1][k]=u-(r.x-1)*z;p[2][k]=(r.x+r.w)*z-u;p[3][k]=v-(r.y-1)*z;p[4][k]=(r.y+r.h)*z-v;}
 for(int j=0;j<5;j++){auto&a=p[j];auto upper=std::max({a[0],a[0]+31*a[1],a[0]+31*(a[1]+a[2])})+std::max(int64_t(0),a[3]);if(j==0?upper<=0:upper<0)return false;}return true;
}
}
