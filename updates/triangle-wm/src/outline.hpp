#pragma once
#include "tile_neighbors.hpp"

// Keep every protected content tile. Peel only background tips attached along
// fewer than two distinct edges. Split edges count once, even with two neighbors.
// Whole tiles remain the unit of ownership; no masks or pixel clipping.
template<class Protected>
static void trim_outline(std::vector<unsigned>& ids, Protected protect){
 std::array<uint8_t,4220> present{},guarded{};
 for(unsigned t:ids)present[t]=1;
 std::vector<unsigned> remove;
 for(;;){
  remove.clear();
  for(unsigned t:ids)if(present[t]&&!guarded[t]){
   unsigned support=0;
   for(auto &edge:tile_neighbors[t])
    support+=present[edge[0]]||(edge[1]!=65535&&present[edge[1]]);
   if(support<2){if(protect(t))guarded[t]=1;else remove.push_back(t);}
  }
  if(remove.empty())break;
  for(unsigned t:remove)present[t]=0;
 }
 ids.erase(std::remove_if(ids.begin(),ids.end(),[&](unsigned t){return !present[t];}),ids.end());
}
