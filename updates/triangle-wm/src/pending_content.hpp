#pragma once

// Bounded damage for the latest WM-owned source image. Separate regions stay
// separate; overlapping rectangles may conservatively include a small gap.
struct PendingContent {
  std::vector<DamageRect> regions;
  uint64_t commits=0;
  void add(DamageRect r) {
    if(!r.w||!r.h)return;
    for(size_t i=0;i<regions.size();) {
      const auto a=regions[i];
      if(a.x<=r.x&&a.y<=r.y&&a.x+a.w>=r.x+r.w&&a.y+a.h>=r.y+r.h)return;
      bool overlap=intersect(a,r).w&&intersect(a,r).h;
      bool rows=a.x==r.x&&a.w==r.w&&a.y<=r.y+r.h&&r.y<=a.y+a.h;
      bool columns=a.y==r.y&&a.h==r.h&&a.x<=r.x+r.w&&r.x<=a.x+a.w;
      if(overlap||rows||columns){r=unite(a,r);regions.erase(regions.begin()+i);i=0;}
      else ++i;
    }
    if(regions.size()==128){for(auto a:regions)r=unite(r,a);regions.clear();}
    regions.push_back(r);
  }
  void merge(const std::vector<DamageRect> &incoming) {
    // Repeated component damage often has exactly the same sparse layout.
    if(regions.size()==incoming.size()&&std::equal(regions.begin(),regions.end(),incoming.begin(),
      [](DamageRect a,DamageRect b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}))return;
    for(auto r:incoming)add(r);
  }
};
