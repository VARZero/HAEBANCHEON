#pragma once
#include "common.hpp"
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
  size_t i = 0;
  for (; i + 3 <= bytes; i += 3) {
    dst[i] = color[0]; dst[i+1] = color[1]; dst[i+2] = color[2];
  }
  for (size_t k = 0; i < bytes; ++i, ++k) dst[i] = color[k];
}
} // namespace boayo
