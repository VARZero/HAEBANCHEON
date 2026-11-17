#pragma once
#include "common.hpp"
#include <memory>
#include <sys/mman.h>
namespace boayo {
inline thread_local uint64_t rgb_materializations=0;
// Read-only file mapping, shared until all windows and DMA submissions release it.
struct Texture {
  struct Patch { std::shared_ptr<const Texture> source; unsigned src, dst, width, height, src_stride, dst_stride; };
  const uint32_t *pointer; size_t count;
  std::shared_ptr<const Texture> base;
  std::vector<Patch> patches;
  Texture(const uint32_t *p,size_t n):pointer(p),count(n){}
  Texture(std::shared_ptr<const Texture> b,std::vector<Patch> p):pointer(b->data()),count(b->size()),base(std::move(b)),patches(std::move(p)){
    require(base->patches.empty(),"texture recipe nesting");
    for(const auto &x:patches)require(x.source && x.source->patches.empty() && x.width && x.height &&
      x.src_stride>=x.width && x.dst_stride>=x.width &&
      uint64_t(x.src)+uint64_t(x.height-1)*x.src_stride+x.width<=x.source->size() &&
      uint64_t(x.dst)+uint64_t(x.height-1)*x.dst_stride+x.width<=count,"texture patch bounds");
  }
  ~Texture(){if(!base)munmap(const_cast<uint32_t *>(pointer),count*4);}
  Texture(const Texture&)=delete;Texture& operator=(const Texture&)=delete;
  const uint32_t *data() const{return pointer;}
  size_t size() const{return count;}
};
struct WordView {
  const uint32_t *pointer;size_t count;
  WordView(const Words &w):pointer(w.data()),count(w.size()){}
  WordView(const Texture &t):pointer(t.data()),count(t.size()){}
  const uint32_t *data()const{return pointer;}size_t size()const{return count;}
  bool empty()const{return count==0;}
};
inline void materialize_rgb(Bytes &rgb,const Texture &texture){
  if(!rgb.empty())return;
  ++rgb_materializations;rgb.resize(texture.size()*3);
  for(size_t i=0;i<texture.size();++i){auto c=texture.data()[i];
    rgb[i*3]=c>>16;rgb[i*3+1]=c>>8;rgb[i*3+2]=c;}
  for(const auto &p:texture.patches)for(unsigned y=0;y<p.height;++y)for(unsigned x=0;x<p.width;++x){
    auto c=p.source->data()[p.src+y*p.src_stride+x];size_t i=p.dst+y*p.dst_stride+x;
    rgb[i*3]=c>>16;rgb[i*3+1]=c>>8;rgb[i*3+2]=c;
  }
}
}
