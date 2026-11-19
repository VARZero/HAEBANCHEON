#pragma once
#include "texture.hpp"
namespace boayo {
struct Rect {int x=0,y=0,w=0,h=0;};
inline constexpr int source_tile_edge=32;
inline Rect intersect(Rect a,Rect b){int x=std::max(a.x,b.x),y=std::max(a.y,b.y);return {x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};}
inline Rect bounds(Rect a,Rect b){if(!a.w || !a.h)return b;if(!b.w || !b.h)return a;int x=std::min(a.x,b.x),y=std::min(a.y,b.y);return {x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y};}

// The caller validates the image and patch dimensions. Keep the damage from
// each tile separate so sparse changes across the image do not dirty the gap.
inline std::vector<Rect> patch_rgb_tiles(Bytes &image, int stride, Rect patch, const Bytes &rgb) {
 std::vector<Rect> changed;
 const int edge=source_tile_edge;
 for(int ty=patch.y/edge;ty<=(patch.y+patch.h-1)/edge;++ty)
  for(int tx=patch.x/edge;tx<=(patch.x+patch.w-1)/edge;++tx){
   Rect part=intersect(patch,{tx*edge,ty*edge,edge,edge}), damage{};
   for(int y=part.y;y<part.y+part.h;++y){
    auto src=rgb.data()+(size_t(y-patch.y)*patch.w+part.x-patch.x)*3;
    auto dst=image.data()+(size_t(y)*stride+part.x)*3;
    if(std::equal(src,src+part.w*3,dst))continue;
    for(int x=part.x;x<part.x+part.w;++x,src+=3,dst+=3){
     if(std::equal(src,src+3,dst))continue;
     std::copy_n(src,3,dst);
     damage=bounds(damage,{x,y,1,1});
    }
   }
   if(damage.w)changed.push_back(damage);
  }
 return changed;
}
inline void add_rect(std::vector<Rect>&rs,Rect r){
 if(r.w<=0||r.h<=0)return;
 // Coalesce overlap/touch without joining distant widgets. Bound metadata growth.
 for(size_t i=0;i<rs.size();) {auto a=rs[i];
   bool join=r.x<=a.x+a.w && a.x<=r.x+r.w && r.y<=a.y+a.h && a.y<=r.y+r.h;
   if(join){r=bounds(r,a);rs.erase(rs.begin()+i);i=0;}else ++i;
 }
 rs.push_back(r);if(rs.size()>128){Rect all{};for(auto a:rs)all=bounds(all,a);rs={all};}
}
// 32x32 source-image damage bins. Bounds stay within each bin; a chain of
// distant widgets cannot inflate into one window-wide dirty rectangle.
// Preserve disjoint subrectangles inside each bin; never fill a gap merely
// because two updates share a tile (that would re-upload the background).
inline std::vector<Rect> tiled_regions(const std::vector<Rect>&input,unsigned w,unsigned h){
 std::map<unsigned,std::vector<Rect>> bins;
 const unsigned edge=source_tile_edge,columns=(w+edge-1)/edge;
 for(auto r:input){r=intersect(r,{0,0,int(w),int(h)});if(!r.w||!r.h)continue;
  if(r.x==0 && r.y==0 && unsigned(r.w)==w && unsigned(r.h)==h)return {r};
  for(unsigned y=r.y/edge;y<=unsigned(r.y+r.h-1)/edge;++y)
   for(unsigned x=r.x/edge;x<=unsigned(r.x+r.w-1)/edge;++x){unsigned id=y*columns+x;
    auto q=intersect(r,{int(x*edge),int(y*edge),int(edge),int(edge)});bins[id].push_back(q);}
 }
 std::vector<Rect> pieces;for(auto &[id,rs]:bins)for(auto r:rs)pieces.push_back(r);
 std::sort(pieces.begin(),pieces.end(),[](Rect a,Rect b){return std::tie(a.y,a.h,a.x)<std::tie(b.y,b.h,b.x);});
 std::vector<Rect> rows;
 for(auto r:pieces){if(!rows.empty()){auto &a=rows.back();if(a.y==r.y && a.h==r.h && a.x+a.w>=r.x){a.w=std::max(a.x+a.w,r.x+r.w)-a.x;continue;}}rows.push_back(r);}
 std::map<std::pair<int,int>,size_t> last;std::vector<Rect> out;
 for(auto r:rows){auto key=std::make_pair(r.x,r.w);auto it=last.find(key);
  if(it!=last.end() && out[it->second].y+out[it->second].h>=r.y){auto &a=out[it->second];a.h=std::max(a.y+a.h,r.y+r.h)-a.y;continue;}
  last[key]=out.size();out.push_back(r);}
 return out;
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
  for(auto p:{a,b})if(p){if(p->dst_stride!=w || p->dst%w+p->width>w)return {{0,0,int(w),int(h)}};dirty.push_back(patch_rect(*p,w));}
 }
 return tiled_regions(dirty,w,h);
}
// Conservative descriptor/rectangle intersection, including sampling margin.
inline bool rect_affects(const std::array<uint32_t,18>&d,Rect r){
 std::array<std::array<int64_t,4>,5> p;
 for(int k=0;k<4;k++){int64_t u=int32_t(d[6+k]),v=int32_t(d[10+k]),z=int32_t(d[14+k]);p[0][k]=z;p[1][k]=u-(r.x-1)*z;p[2][k]=(r.x+r.w)*z-u;p[3][k]=v-(r.y-1)*z;p[4][k]=(r.y+r.h)*z-v;}
 for(int j=0;j<5;j++){auto&a=p[j];auto upper=std::max({a[0],a[0]+31*a[1],a[0]+31*(a[1]+a[2])})+std::max(int64_t(0),a[3]);if(j==0?upper<=0:upper<0)return false;}return true;
}
}
