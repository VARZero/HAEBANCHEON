#pragma once
// Screen-space sizes. The paint extent can cross a triangle edge.
namespace wm_controls {
constexpr int icon_radius=14,plate_radius=18,hit_radius=24;
constexpr int icon_size=28,hit_size=48;
inline bool ink(int kind,int x,int y){
 const int ax=std::abs(x),ay=std::abs(y);
 if(kind==1)return ax<=12&&ay<=12&&(std::abs(x-y)<=2||std::abs(x+y)<=2);
 if(kind==2)return (ax<=2&&ay<=14)||(ay<=2&&ax<=14)||
  (ax>=9&&ax<=14&&std::abs(ay-(14-ax))<=2)||
  (ay>=9&&ay<=14&&std::abs(ax-(14-ay))<=2);
 return ax<=13&&ay<=13&&(std::abs(x-y)<=2||
  (x>=7&&y>=7&&(x>=11||y>=11))||(x<=-7&&y<=-7&&(x<=-11||y<=-11)));
}
}
