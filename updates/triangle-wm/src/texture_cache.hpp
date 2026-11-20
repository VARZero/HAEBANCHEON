#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>
#include <stdexcept>

// Word offsets into the existing 4 MiB BO_TEXTURE region. Runs are synchronous:
// an entry may be evicted only between completed projector batches.
struct TextureCache {
 struct Entry {unsigned offset,words;uint64_t revision=0,used=0;bool valid=false;};
 unsigned capacity=1048576,evictions=0;uint64_t tick=0;
 std::map<int,Entry> entries;
 void erase(int id){entries.erase(id);}
 void clear(){entries.clear();}
 Entry &acquire(int id,unsigned words){
  if(!words || words>capacity)throw std::runtime_error("texture exceeds arena");
  auto found=entries.find(id);
  if(found!=entries.end() && found->second.words!=words){entries.erase(found);found=entries.end();}
  if(found!=entries.end()){found->second.used=++tick;return found->second;}
  for(;;){
   std::vector<const Entry*> ordered;ordered.reserve(entries.size());
   for(auto &[key,e]:entries)ordered.push_back(&e);
   std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->offset<b->offset;});
   unsigned start=0;
   for(auto e:ordered){if(e->offset-start>=words)break;start=e->offset+e->words;}
   if(words<=capacity-start)return entries.emplace(id,Entry{start,words,0,++tick,false}).first->second;
   auto victim=std::min_element(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.second.used<b.second.used;});
   if(victim==entries.end())throw std::runtime_error("texture allocation failed");
   entries.erase(victim);++evictions;
  }
 }
};
