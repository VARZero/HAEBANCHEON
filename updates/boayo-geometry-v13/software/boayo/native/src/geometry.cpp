#include "geometry.hpp"
#include "geometry_math.hpp"
#include <cstring>
namespace boayo {
#include "fixed_geometry.inc"
Geometry::Geometry() : faces(fixed_faces), triangles(fixed_triangles) {}
std::array<int32_t, 180> Geometry::camera(double yaw, double pitch, double roll, double fh,
                                          double fv) const {
  require(fh > 0 && fh < 160 && fv > 0 && fv < 160, "invalid camera FOV");
  if (yaw == 0 && pitch == 0 && roll == 0 && fh == 48 && fv == 36)
    return fixed_camera;
  auto a = axes(yaw, pitch, roll);
  V dx = a[1] * (2 * std::tan(rad(fh / 2)) / 1280), dy = a[2] * (-2 * std::tan(rad(fv / 2)) / 720),
    start = a[0] - dx * (1279. / 2) - dy * (719. / 2);
  std::array<int32_t, 180> out{};
  for (unsigned f = 0; f < 20; f++) {
    V v0 = faces[f][0], v1 = faces[f][1], v2 = faces[f][2];
    double det = dot(v0, cross(v1, v2));
    std::array<V, 3> inverse = {cross(v1, v2) * (1 / det), cross(v2, v0) * (1 / det),
                                cross(v0, v1) * (1 / det)};
    for (unsigned j = 0; j < 3; j++) {
      out[f * 9 + j * 3] = int32_t(std::nearbyint(dot(inverse[j], start) * 16777216));
      out[f * 9 + j * 3 + 1] = int32_t(std::nearbyint(dot(inverse[j], dx) * 16777216));
      out[f * 9 + j * 3 + 2] = int32_t(std::nearbyint(dot(inverse[j], dy) * 16777216));
    }
  }
  return out;
}
std::pair<Descriptor, bool> Geometry::descriptor(unsigned id, unsigned w, unsigned h, double yaw,
                                                 double pitch, double roll, double fh,
                                                 double fv) const {
  return descriptor(id, Projection(w, h, yaw, pitch, roll, fh, fv));
}
Geometry::Projection::Projection(unsigned width, unsigned height, double yaw, double pitch,
                                 double roll, double fh, double fv)
    : w(width), h(height) {
  require(w >= 1 && w <= 2048 && h >= 1 && h <= 2048 && fh > 0 && fh < 160 && fv > 0 && fv < 160,
          "invalid dimensions/FOV");
  ax = axes(yaw, pitch, roll);
  tx = 2 * std::tan(rad(fh / 2));
  ty = 2 * std::tan(rad(fv / 2));
}
std::pair<Descriptor, bool> Geometry::descriptor(unsigned id, const Projection &p) const {
  require(id<tiles,"invalid tile");geometry_math::Terms t;
  geometry_math::project(triangles[id],p,t);geometry_math::coefficients(t);
  t.scale=geometry_math::optimized()?geometry_math::scale_fast(t.bound):geometry_math::scale_original(t.bound);
  return geometry_math::finish(id,p,t);
}
#include "geometry_reference.inc"

V Geometry::ray(unsigned id, unsigned cell, unsigned m) const {
  unsigned row = unsigned(std::sqrt(cell)), col = (cell - row * row) / 2,
           inv = (cell - row * row) & 1;
  double s = inv ? 2. / 3 : 1. / 3;
  V b = {(m - 1 - row + s) / m, (row - col - inv + s) / m, (col + s) / m};
  auto &q = triangles.at(id);
  return unit(q[0] * b[0] + q[1] * b[1] + q[2] * b[2]);
}
std::array<int64_t, 3> coordinates(const Descriptor &d, unsigned cell) {
  unsigned row = unsigned(std::sqrt(cell)), col = (cell - row * row) / 2,
           inv = (cell - row * row) & 1;
  std::array<int64_t, 3> q;
  for (int j = 0; j < 3; j++)
    q[j] = int32_t(d[6 + j * 4]) + int64_t(int32_t(d[7 + j * 4])) * row +
           int64_t(int32_t(d[8 + j * 4])) * col + int64_t(int32_t(d[9 + j * 4])) * inv;
  return q;
}
bool covered(const Descriptor &d, const std::array<int64_t, 3> &q) {
  return q[2] > 0 && q[0] >= 0 && q[1] >= 0 && q[0] <= q[2] * (d[2] - 1) &&
         q[1] <= q[2] * (d[3] - 1);
}
unsigned project_reference(const Words &texture, Words &scene, const Descriptor &d) {
  require(uint64_t(d[1]) + 256 <= scene.size() && d[2] && d[3] && d[4] >= d[2] &&
              uint64_t(d[0]) + uint64_t(d[3] - 1) * d[4] + d[2] <= texture.size(),
          "projection bounds");
  auto *out = reinterpret_cast<uint8_t *>(scene.data() + d[1]);
  if (d[5] & 1)
    std::fill(out, out + 1024, uint8_t(d[5] >> 8));
  unsigned written = 0;
  for (unsigned cell = 0; cell < 1024; cell++) {
    auto q = coordinates(d, cell);
    if (!covered(d, q))
      continue;
    ++written;
    uint64_t xq = q[0] * 256 / q[2], yq = q[1] * 256 / q[2];
    unsigned x = xq / 256, y = yq / 256, ax = xq % 256, ay = yq % 256,
             x1 = std::min(d[2] - 1, x + 1), y1 = std::min(d[3] - 1, y + 1);
    uint32_t p[4] = {texture[d[0] + y * d[4] + x], texture[d[0] + y * d[4] + x1],
                     texture[d[0] + y1 * d[4] + x], texture[d[0] + y1 * d[4] + x1]},
             c[3];
    for (unsigned k = 0; k < 3; k++) {
      unsigned shift = 16 - k * 8;
      uint32_t a = (p[0] >> shift) & 255, b = (p[1] >> shift) & 255, v = (p[2] >> shift) & 255,
               e = (p[3] >> shift) & 255;
      c[k] =
          (((a * (256 - ax) + b * ax) * (256 - ay) + (v * (256 - ax) + e * ax) * ay + 32768) >> 16);
    }
    out[cell] = rgb_index(c[0], c[1], c[2]);
  }
  return written;
}
} // namespace boayo
