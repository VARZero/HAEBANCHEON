#pragma once
#include "backend.hpp"
namespace boayo {
inline std::vector<Rect> window_regions(const Window&w){
 if(w.dirty_regions_revision==w.revision && !w.dirty_regions.empty())return w.dirty_regions;
 if(w.dirty[2] && w.dirty[3])return {{w.dirty[0],w.dirty[1],w.dirty[2],w.dirty[3]}};
 return {{0,0,w.data["surface_width"].get<int>(),w.data["surface_height"].get<int>()}};
}
// Existing 4 MiB BO_TEXTURE arena, word-addressed. Runs are synchronous;
// a slot is reusable only after both projectors have completed their previous run.
struct TexturePool {
 struct Slot {unsigned offset=0,words=0,width=0,height=0;uint64_t revision=0,observed=0,used=0;bool valid=false;std::shared_ptr<const Texture> prepared;std::vector<Rect> pending;};
 Device&dev;unsigned capacity;std::map<int,Slot> slots;uint64_t clock=0;
 uint64_t full_uploads=0,partial_uploads=0,reuses=0,evictions=0,upload_bytes=0,upload_calls=0,staged_copy_bytes=0;
 explicit TexturePool(Device&d,unsigned words=0x100000):dev(d),capacity(words){require(words && words<=0x100000,"texture pool capacity");}
 void clear(){slots.clear();}
 void prune(const WM&wm){for(auto i=slots.begin();i!=slots.end();)if(!wm.windows.count(i->first))i=slots.erase(i);else ++i;}
 void note(int id,const Window&w){auto it=slots.find(id);if(it==slots.end())return;auto&s=it->second;
  if(s.width!=w.data["surface_width"] || s.height!=w.data["surface_height"]){slots.erase(it);return;}
  if(s.observed==w.revision)return;
  for(auto r:window_regions(w))add_rect(s.pending,r);s.observed=w.revision;
 }
 unsigned gap(unsigned n)const{
  std::vector<std::pair<unsigned,unsigned>> ranges;for(auto&v:slots)ranges.emplace_back(v.second.offset,v.second.words);
  std::sort(ranges.begin(),ranges.end());unsigned next=0;for(auto p:ranges){if(p.first-next>=n)return next;next=p.first+p.second;}return capacity-next>=n?next:capacity;
 }
 Slot&allocate(int id,unsigned w,unsigned h){uint64_t count=uint64_t(w)*h;require(count && count<=capacity,"window exceeds texture arena");
  slots.erase(id);unsigned off=gap(count);
  while(off==capacity){require(!slots.empty(),"texture allocation");auto victim=std::min_element(slots.begin(),slots.end(),[](const auto&a,const auto&b){return a.second.used<b.second.used;});slots.erase(victim);++evictions;off=gap(count);}
  Slot s;s.offset=off;s.words=count;s.width=w;s.height=h;return slots.emplace(id,std::move(s)).first->second;
 }
 void upload(unsigned region,unsigned off,const uint32_t*p,unsigned n){dev.upload_direct(region,off,p,n);upload_bytes+=uint64_t(n)*4;++upload_calls;}
 void rectangle(Slot&s,const uint32_t*source,unsigned stride,Rect r,unsigned dst_x,unsigned dst_y){
  require(r.w>0 && r.h>0 && uint64_t(dst_x)+r.w<=s.width && uint64_t(dst_y)+r.h<=s.height,"texture patch destination");
  const uint32_t*start=source+size_t(r.y)*stride+r.x;unsigned dst=s.offset+dst_y*s.width+dst_x;
  if(unsigned(r.w)==stride && stride==s.width){upload(BO_TEXTURE,dst,start,unsigned(r.w)*r.h);return;}
  unsigned span=(r.h-1)*stride+r.w;
  // One pinned-page upload, then ordered 2D copies. Scratch is idle here and
  // all copies finish before projection reuses it for boundary tiles.
  if(r.h>1 && span<=BO_SCRATCH_BYTES/4 && uint64_t(span)<=uint64_t(r.w)*r.h*2){
   upload(BO_SCRATCH,0,start,span);std::vector<bo_batch_entry> commands;
   for(int y=0;y<r.h;y++){bo_batch_entry b{};b.kind=BO_BATCH_COPY;b.op.copy={BO_SCRATCH,unsigned(y)*stride*4,BO_TEXTURE,(dst+unsigned(y)*s.width)*4,unsigned(r.w)*4,0};commands.push_back(b);}
   dev.batch(commands);staged_copy_bytes+=uint64_t(r.w)*r.h*4;
  }else for(int y=0;y<r.h;y++)upload(BO_TEXTURE,dst+y*s.width,start+size_t(y)*stride,r.w);
 }
 void prepared_rectangle(Slot&s,const Texture&t,Rect area){
  const auto*base=texture_base(t);int first=-1;
  for(unsigned i=0;i<t.patches.size();++i){const auto&p=t.patches[i];require(p.dst_stride==s.width && p.dst%s.width+p.width<=s.width,"texture recipe row layout");auto b=patch_rect(p,s.width);auto overlap=intersect(area,b);if(overlap.x==area.x && overlap.y==area.y && overlap.w==area.w && overlap.h==area.h)first=i;}
  if(first<0)rectangle(s,base->data(),s.width,area,area.x,area.y);
  for(size_t i=first<0?0:size_t(first);i<t.patches.size();++i){const auto&p=t.patches[i];auto b=patch_rect(p,s.width),q=intersect(area,b);if(!q.w || !q.h)continue;
   Rect src{int(p.src%p.src_stride)+q.x-b.x,int(p.src/p.src_stride)+q.y-b.y,q.w,q.h};rectangle(s,p.source->data(),p.src_stride,src,q.x,q.y);
  }
 }
 Slot&ensure(int id,const Window&w,const Layer&layer){
  unsigned width=w.data["surface_width"],height=w.data["surface_height"];note(id,w);
  auto it=slots.find(id);Slot&s=it==slots.end()?allocate(id,width,height):it->second;s.used=++clock;
  if(s.valid && s.revision==w.revision){++reuses;return s;}
  bool full=!s.valid;std::vector<Rect> dirty;
  if(full)dirty={{0,0,int(width),int(height)}};
  else if(w.prepared && s.prepared)dirty=texture_delta(s.prepared.get(),*w.prepared,width,height);
  else dirty=s.pending.empty()?std::vector<Rect>{{0,0,int(width),int(height)}}:s.pending;
  if(dirty.empty()){s.prepared=w.prepared;s.revision=s.observed=w.revision;s.pending.clear();++reuses;return s;}
  s.valid=false;
  try{for(auto r:dirty){r=intersect(r,{0,0,int(width),int(height)});if(!r.w || !r.h)continue;
    if(w.prepared)prepared_rectangle(s,*w.prepared,r);
    else{require(layer.source.size()==s.words,"CPU texture size");rectangle(s,layer.source.data(),width,r,r.x,r.y);}
   }
   s.valid=true;s.revision=s.observed=w.revision;s.prepared=w.prepared;s.pending.clear();if(full)++full_uploads;else ++partial_uploads;
  }catch(...){slots.erase(id);throw;}
  return s;
 }
 J status()const{unsigned used=0;for(auto&v:slots)used+=v.second.words;return {{"capacity_bytes",capacity*4},{"resident_bytes",used*4},{"resident_windows",slots.size()},{"full_uploads",full_uploads},{"partial_uploads",partial_uploads},{"reuses",reuses},{"evictions",evictions},{"upload_bytes",upload_bytes},{"upload_calls",upload_calls},{"staged_copy_bytes",staged_copy_bytes}};}
};
}
