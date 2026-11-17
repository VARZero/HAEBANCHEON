#pragma once
#include "wm.hpp"
#include "geometry.hpp"
extern "C" {
void *bosio_compositor_create(const float *, uint32_t);
void bosio_compositor_destroy(void *);
int bosio_compositor_sync_window_dirty(void *, uint64_t, float, float, float, float, float,
                                       uint32_t, uint32_t, uint64_t, const uint8_t *, uint32_t,
                                       uint32_t, uint32_t, uint32_t);
void bosio_compositor_set_shape(void *, uint64_t, const float *, uint32_t);
void bosio_compositor_remove_window(void *, uint64_t);
int bosio_compositor_render_packed(void *, const uint64_t *, uint32_t, uint64_t, float, float, int,
                                   uint8_t, uint8_t, uint8_t, uint32_t, uint32_t *, uint32_t,
                                   uint32_t *);
int bosio_compositor_render(void *, const uint64_t *, uint32_t, uint64_t, float, float, int,
                            uint8_t, uint8_t, uint8_t, uint8_t *);
int bosio_pack_scene(const uint8_t *, uint32_t, uint32_t *, uint32_t, uint32_t *);
}
namespace boayo {
class NativeRenderer {
  void *context = nullptr;
  std::set<int> old_ids;

public:
  explicit NativeRenderer(const Geometry &g) {
    std::vector<float> rays(size_t(tiles) * 1024 * 3);
    for (unsigned t = 0; t < tiles; t++)
      for (unsigned i = 0; i < 1024; i++) {
        auto q = g.ray(t, i);
        for (unsigned k = 0; k < 3; k++)
          rays[(size_t(t) * 1024 + i) * 3 + k] = q[k];
      }
    context = bosio_compositor_create(rays.data(), tiles * 1024);
    require(context, "native renderer allocation failed");
  }
  ~NativeRenderer() {
    if (context)
      bosio_compositor_destroy(context);
  }
  std::vector<uint64_t> sync(WM &wm) {
    for (int id : old_ids)
      if (!wm.windows.count(id))
        bosio_compositor_remove_window(context, id);
    old_ids.clear();
    std::vector<uint64_t> order;
    for (auto &[id, w] : wm.windows) {
      old_ids.insert(id);
      auto &d = w.data;
      auto r = w.dirty;
      int rc = bosio_compositor_sync_window_dirty(
          context, id, d["azimuth"], d["elevation"], d["roll"], d["width_deg"], d["height_deg"],
          d["surface_width"], d["surface_height"], w.revision, w.surface.data(), r[0], r[1], r[2],
          r[3]);
      require(rc == 0, "native renderer window sync failed");
      std::vector<float> shape;
      for (auto &rect : d["shape"])
        for (auto &v : rect)
          shape.push_back(v.get<float>());
      bosio_compositor_set_shape(context, id, shape.data(), shape.size() / 5);
    }
    for (int id : wm.order)
      if (wm.windows[id].data["mapped"].get<bool>())
        order.push_back(id);
    return order;
  }
  Words packed(WM &wm) {
    auto order = sync(wm);
    Words out(1084816);
    uint32_t active = 0;
    int n = bosio_compositor_render_packed(context, order.data(), order.size(), wm.focus, wm.paz,
                                           wm.pel, wm.pointer_visible, 2, 6, 14, 32, out.data(),
                                           out.size(), &active);
    require(n > 0, "native render failed");
    out.resize(n);
    for (auto &[id, w] : wm.windows)
      w.dirty = {};
    return out;
  }
  Bytes rgb(WM &wm) {
    auto order = sync(wm);
    Bytes out(size_t(tiles) * 1024 * 3);
    require(bosio_compositor_render(context, order.data(), order.size(), wm.focus, wm.paz, wm.pel,
                                    wm.pointer_visible, 2, 6, 14, out.data()) == 0,
            "native RGB render failed");
    return out;
  }
};
inline Bytes unpack_scene(const Words &s) {
  require(s.size() >= header && s.size() <= 1084816, "invalid packed scene length");
  Bytes rgb(size_t(tiles) * 1024 * 3);
  auto *data = reinterpret_cast<const uint8_t *>(s.data() + header);
  size_t count = (s.size() - header) * 4;
  for (unsigned t = 0; t < tiles; t++) {
    uint32_t off = s[256 + t];
    if (off == 0xffffffff)
      continue;
    require(uint64_t(off) + 1024 <= count, "packed scene directory out of bounds");
    for (unsigned i = 0; i < 1024; i++) {
      auto c = s[data[off + i]];
      size_t p = (size_t(t) * 1024 + i) * 3;
      rgb[p] = c >> 16;
      rgb[p + 1] = c;
      rgb[p + 2] = c >> 8;
    }
  }
  return rgb;
}
inline Words pack_rgb(const Bytes &rgb) {
  require(rgb.size() == size_t(tiles) * 1024 * 3, "invalid sphere RGB size");
  Words s(1084816);
  uint32_t active;
  int n = bosio_pack_scene(rgb.data(), 32, s.data(), s.size(), &active);
  require(n > 0, "scene packing failed");
  s.resize(n);
  return s;
}
} // namespace boayo
