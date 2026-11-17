#pragma once
#include "common.hpp"
#include <cstring>
namespace boayo {
// Only the render thread owns the mutable surfaces and the exclusive device.
struct RgbFillHook {
  void *context = nullptr;
  bool (*fill)(void *, uint8_t *, size_t, const std::array<uint8_t, 3> &) = nullptr;
};
inline thread_local RgbFillHook rgb_fill_hook;
struct RgbFillScope {
  RgbFillHook previous;
  explicit RgbFillScope(RgbFillHook hook) : previous(rgb_fill_hook) { rgb_fill_hook = hook; }
  ~RgbFillScope() { rgb_fill_hook = previous; }
  RgbFillScope(const RgbFillScope &) = delete;
  RgbFillScope &operator=(const RgbFillScope &) = delete;
};
inline void fill_rgb(uint8_t *dst, size_t bytes, const std::array<uint8_t, 3> &color) {
  if (bytes >= 4096 && !(reinterpret_cast<uintptr_t>(dst) & 3) && !(bytes & 3) &&
      rgb_fill_hook.fill && rgb_fill_hook.fill(rgb_fill_hook.context, dst, bytes, color))
    return;
  // Small/unaligned buffers and old kernels retain an exact CPU fallback.
  size_t filled=std::min(bytes,size_t(12));
  for(size_t i=0;i<filled;++i)dst[i]=color[i%3];
  while(filled<bytes) {
    size_t n=std::min(filled,bytes-filled);
    // Each completed prefix is a multiple of the RGB period; ranges do not overlap.
    std::memcpy(dst+filled,dst,n);filled+=n;
  }
}
} // namespace boayo
