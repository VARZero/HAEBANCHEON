#pragma once
#include "common.hpp"
#include <memory>
#include <sys/mman.h>
namespace boayo {
inline thread_local uint64_t rgb_materializations=0;
// Read-only file mapping, shared until all windows and DMA submissions release it.
struct Texture {
  const uint32_t *pointer; size_t count;
  Texture(const uint32_t *p,size_t n):pointer(p),count(n){}
  ~Texture(){munmap(const_cast<uint32_t *>(pointer),count*4);}
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
}
}
