#pragma once
#include "geometry.hpp"
namespace boayo {
// Compile JSON once per shape. Only validated integral rectangles use the
// homogeneous fast path; fractional shapes retain the original double formula.
struct MaskShape {
  struct Rect {
    double x,y,w,h,r;
    int64_t left=0,top=0,right=0,bottom=0,inner_left=0,inner_top=0,inner_right=0,inner_bottom=0;
  };
  std::vector<Rect> rects;
  bool integral=true;
  uint64_t integer_samples=0,precise_samples=0;
  explicit MaskShape(const J &shape) {
    for(const auto &a:shape) {
      Rect r{a[0].get<double>(),a[1].get<double>(),a[2].get<double>(),a[3].get<double>(),a[4].get<double>()};
      bool integer=true;
      for(double v:{r.x,r.y,r.w,r.h,r.r})
        integer &= std::isfinite(v) && v>=0 && v<=4096 && std::floor(v)==v;
      integer &= r.x+r.w<=4096 && r.y+r.h<=4096;
      integral &= integer;
      if(integer) {
        r.left=int64_t(r.x);r.top=int64_t(r.y);r.right=int64_t(r.x+r.w);r.bottom=int64_t(r.y+r.h);
        r.inner_left=int64_t(r.x+r.r);r.inner_top=int64_t(r.y+r.r);
        r.inner_right=int64_t(r.x+r.w-r.r);r.inner_bottom=int64_t(r.y+r.h-r.r);
      }
      rects.push_back(r);
    }
  }
  static bool bounded(const std::array<int64_t,3> &q) {
    constexpr int64_t limit=int64_t(1)<<40;
    return q[2]>0 && q[2]<=limit && q[0]>=0 && q[0]<=limit && q[1]>=0 && q[1]<=limit;
  }
  static bool box(const Rect &r,const std::array<int64_t,3>&q) {
    return q[0]>=r.left*q[2] && q[0]<r.right*q[2] && q[1]>=r.top*q[2] && q[1]<r.bottom*q[2];
  }
  static bool strip_x(const Rect&r,const std::array<int64_t,3>&q) {
    return q[0]>=r.inner_left*q[2] && q[0]<=r.inner_right*q[2];
  }
  static bool strip_y(const Rect&r,const std::array<int64_t,3>&q) {
    return q[1]>=r.inner_top*q[2] && q[1]<=r.inner_bottom*q[2];
  }
  static bool exact(const Rect&r,double x,double y) {
    // Keep expression order and inclusive circular / half-open box boundaries.
    double dx=std::max({r.x+r.r-x,x-(r.x+r.w-r.r),0.});
    double dy=std::max({r.y+r.r-y,y-(r.y+r.h-r.r),0.});
    return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h && dx*dx+dy*dy<=r.r*r.r;
  }
  bool contains(const std::array<int64_t,3>&q) {
    if(rects.empty())return true;
    bool fast=integral && bounded(q),converted=false;
    double x=0,y=0;
    if(fast)++integer_samples;
    for(const auto&r:rects) {
      if(fast) {
        if(!box(r,q))continue;
        if(strip_x(r,q)||strip_y(r,q))return true;
      }
      if(!converted){x=double(q[0])/q[2];y=double(q[1])/q[2];converted=true;++precise_samples;}
      if(exact(r,x,y))return true;
    }
    return false;
  }
  bool full_tile(const Descriptor &d) const {
    if(!integral)return false;
    // The actual triangular sample lattice is contained in this box. All
    // inequalities are affine in (row,col,inv), so its eight vertices suffice.
    std::array<std::array<int64_t,3>,8> corners{};
    for(unsigned k=0;k<8;++k) {
      for(unsigned j=0;j<3;++j)
        corners[k][j]=int64_t(int32_t(d[6+4*j]))+int64_t(int32_t(d[7+4*j]))*((k&1)?31:0)
          +int64_t(int32_t(d[8+4*j]))*((k&2)?31:0)+int64_t(int32_t(d[9+4*j]))*((k&4)?1:0);
      if(!bounded(corners[k]) || !covered(d,corners[k]))return false;
    }
    if(rects.empty())return true;
    for(const auto&r:rects) {
      bool inside=true,x=true,y=true;
      for(const auto&q:corners){inside &= box(r,q);x &= strip_x(r,q);y &= strip_y(r,q);}
      // One entire convex strip must contain the tile; never accept a union
      // merely because different vertices happen to belong to different strips.
      if(inside && (x||y))return true;
    }
    return false;
  }
};
inline uint8_t prepare_mask(const Descriptor&d,MaskShape &shape,Bytes&mask,std::array<uint32_t,32>&packed,bool &whole) {
  packed.fill(0);whole=shape.full_tile(d);
  if(whole){std::fill(mask.begin(),mask.end(),1);packed.fill(~uint32_t(0));return 1;}
  bool any=false,all=true;
  std::array<int64_t,3> row_base{},column{},inverted{},row_delta{};
  for(unsigned j=0;j<3;++j){row_base[j]=int32_t(d[6+4*j]);row_delta[j]=int32_t(d[7+4*j]);column[j]=int32_t(d[8+4*j]);inverted[j]=int32_t(d[9+4*j]);}
  unsigned cell=0;
  auto sample=[&](const std::array<int64_t,3>&q){
    bool yes=covered(d,q) && shape.contains(q);mask[cell]=yes;
    if(yes)packed[cell/32]|=uint32_t(1)<<(cell%32);
    any|=yes;all&=yes;++cell;
  };
  // 1,3,...,63 samples per row: no square root or multiplication per sample.
  for(unsigned row=0;row<32;++row){auto q=row_base;
    for(unsigned col=0;col<=row;++col){sample(q);
      if(col<row){auto odd=q;for(unsigned j=0;j<3;++j)odd[j]+=inverted[j];sample(odd);}
      for(unsigned j=0;j<3;++j)q[j]+=column[j];
    }
    for(unsigned j=0;j<3;++j)row_base[j]+=row_delta[j];
  }
  return !any?0:all?1:2;
}
} // namespace boayo
