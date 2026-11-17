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
 struct AssetSlot {std::shared_ptr<const Texture> source;unsigned offset=0,words=0;uint64_t used=0;};
 std::map<const Texture*,AssetSlot> assets;std::vector<bo_batch_entry> copies;
 uint64_t asset_hits=0,asset_misses=0,asset_evictions=0,asset_upload_bytes=0,copy_bytes=0,copy_requests=0,copy_batches=0,tiles_updated=0;
 Device&dev;unsigned capacity;std::map<int,Slot> slots;uint64_t clock=0;
 uint64_t full_uploads=0,partial_uploads=0,reuses=0,evictions=0,upload_bytes=0,upload_calls=0,staged_copy_bytes=0;
 explicit TexturePool(Device&d,unsigned words=0x100000):dev(d),capacity(words){require(words && words<=0x100000,"texture pool capacity");}
 void clear(){require(copies.empty(),"pending texture copies");slots.clear();assets.clear();}
 void prune(const WM&wm){for(auto i=slots.begin();i!=slots.end();)if(!wm.windows.count(i->first))i=slots.erase(i);else ++i;}
 void note(int id,const Window&w){auto it=slots.find(id);if(it==slots.end())return;auto&s=it->second;
  if(s.width!=w.data["surface_width"] || s.height!=w.data["surface_height"]){slots.erase(it);return;}
  if(s.observed==w.revision)return;
  for(auto r:window_regions(w))s.pending.push_back(r);s.pending=tiled_regions(s.pending,s.width,s.height);s.observed=w.revision;
 }
 unsigned gap(unsigned n)const{
  std::vector<std::pair<unsigned,unsigned>> ranges;for(auto&v:slots)ranges.emplace_back(v.second.offset,v.second.words);
  for(auto &v:assets)ranges.emplace_back(v.second.offset,v.second.words);
  std::sort(ranges.begin(),ranges.end());unsigned next=0;for(auto p:ranges){if(p.first-next>=n)return next;next=p.first+p.second;}return capacity-next>=n?next:capacity;
 }
 Slot&allocate(int id,unsigned w,unsigned h){uint64_t count=uint64_t(w)*h;require(count && count<=capacity,"window exceeds texture arena");
  slots.erase(id);unsigned off=gap(count);
  while(off==capacity && !assets.empty()){evict_asset();off=gap(count);}
  while(off==capacity){require(!slots.empty(),"texture allocation");auto victim=std::min_element(slots.begin(),slots.end(),[](const auto&a,const auto&b){return a.second.used<b.second.used;});slots.erase(victim);++evictions;off=gap(count);}
  Slot s;s.offset=off;s.words=count;s.width=w;s.height=h;return slots.emplace(id,std::move(s)).first->second;
 }
 void flush_copies(){if(copies.empty())return;dev.batch(copies);++copy_batches;copies.clear();}
 void evict_asset(){require(copies.empty(),"asset still referenced by queued DMA");auto v=std::min_element(assets.begin(),assets.end(),[](const auto&a,const auto&b){return a.second.used<b.second.used;});require(v!=assets.end(),"asset eviction");assets.erase(v);++asset_evictions;}
 unsigned cached_asset(const std::shared_ptr<const Texture>&t){
  auto found=assets.find(t.get());if(found!=assets.end()){found->second.used=++clock;++asset_hits;return found->second.offset;}
  if(t->size()>16384)return capacity;
  unsigned budget=std::min(65536u,capacity/8),n=t->size();
  if(!n || n>16384 || n>budget || !t->patches.empty())return capacity;
  flush_copies();++asset_misses;
  unsigned used=0;for(auto &v:assets)used+=v.second.words;
  while(!assets.empty() && used+n>budget){evict_asset();used=0;for(auto &v:assets)used+=v.second.words;}
  unsigned off=gap(n);while(off==capacity && !assets.empty()){evict_asset();off=gap(n);}
  if(off==capacity)return capacity; // Never evict a live window for an asset.
  upload(BO_TEXTURE,off,t->data(),n);asset_upload_bytes+=uint64_t(n)*4;
  assets.emplace(t.get(),AssetSlot{t,off,n,++clock});return off;
 }
 void queue_copy(unsigned src,unsigned dst,unsigned words){
  copy_bytes+=uint64_t(words)*4;
  if(!copies.empty()){auto &last=copies.back().op.copy;
   if(last.src_offset+last.bytes==src*4 && last.dst_offset+last.bytes==dst*4){last.bytes+=words*4;return;}}
  bo_batch_entry b{};b.kind=BO_BATCH_COPY;b.op.copy={BO_TEXTURE,src*4,BO_TEXTURE,dst*4,words*4,0};copies.push_back(b);++copy_requests;
  if(copies.size()==BO_BATCH_MAX)flush_copies();
 }
 void asset_rectangle(Slot&s,const std::shared_ptr<const Texture>&t,unsigned stride,Rect r,unsigned x,unsigned y){
  unsigned off=cached_asset(t);
  if(off==capacity){rectangle(s,t->data(),stride,r,x,y);return;}
  for(int row=0;row<r.h;++row)queue_copy(off+(r.y+row)*stride+r.x,s.offset+(y+row)*s.width+x,r.w);
 }
 void upload(unsigned region,unsigned off,const uint32_t*p,unsigned n){dev.upload_direct(region,off,p,n);upload_bytes+=uint64_t(n)*4;++upload_calls;}
 void rectangle(Slot&s,const uint32_t*source,unsigned stride,Rect r,unsigned dst_x,unsigned dst_y){
  flush_copies();
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
   Rect src{int(p.src%p.src_stride)+q.x-b.x,int(p.src/p.src_stride)+q.y-b.y,q.w,q.h};asset_rectangle(s,p.source,p.src_stride,src,q.x,q.y);
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
  dirty=tiled_regions(dirty,width,height);
  s.valid=false;
  try{for(auto r:dirty){r=intersect(r,{0,0,int(width),int(height)});if(!r.w || !r.h)continue;
    if(w.prepared)prepared_rectangle(s,*w.prepared,r);
    else{require(layer.source.size()==s.words,"CPU texture size");rectangle(s,layer.source.data(),width,r,r.x,r.y);}
   }
   flush_copies();
   std::set<unsigned> changed;unsigned columns=(width+31)/32;
   for(auto r:dirty)for(unsigned y=r.y/32;y<=unsigned(r.y+r.h-1)/32;++y)for(unsigned x=r.x/32;x<=unsigned(r.x+r.w-1)/32;++x)changed.insert(y*columns+x);
   tiles_updated+=changed.size();
   s.valid=true;s.revision=s.observed=w.revision;s.prepared=w.prepared;s.pending.clear();if(full)++full_uploads;else ++partial_uploads;
  }catch(...){copies.clear();slots.erase(id);throw;}
  return s;
 }
 J status()const{unsigned used=0,cached=0;for(auto&v:slots)used+=v.second.words;for(auto&v:assets)cached+=v.second.words;return {{"asset_resident_bytes",cached*4},{"total_resident_bytes",(used+cached)*4},{"source_tile_size",32},{"source_tiles_updated",tiles_updated},{"asset_cache_hits",asset_hits},{"asset_cache_misses",asset_misses},{"asset_cache_evictions",asset_evictions},{"asset_cache_upload_bytes",asset_upload_bytes},{"asset_copy_bytes",copy_bytes},{"asset_copy_requests",copy_requests},{"asset_copy_batches",copy_batches},{"capacity_bytes",capacity*4},{"resident_bytes",used*4},{"resident_windows",slots.size()},{"full_uploads",full_uploads},{"partial_uploads",partial_uploads},{"reuses",reuses},{"evictions",evictions},{"upload_bytes",upload_bytes},{"upload_calls",upload_calls},{"staged_copy_bytes",staged_copy_bytes}};}
};
}
