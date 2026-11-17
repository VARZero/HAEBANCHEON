#pragma once
#include "common.hpp"
namespace boayo {
using Triangle = std::array<V, 3>;
using Descriptor = std::array<uint32_t, 18>;
struct Geometry {
  const std::array<Triangle, 20> &faces;
  const std::array<Triangle, tiles> &triangles;
  Geometry();
  struct Projection {
    unsigned w, h;
    std::array<V, 3> ax;
    double tx, ty;
    Projection(unsigned width, unsigned height, double yaw, double pitch, double roll, double fh,
               double fv);
  };
  std::pair<Descriptor, bool> descriptor(unsigned id, const Projection &p) const;
  std::array<int32_t, 180> camera(double yaw = 0, double pitch = 0, double roll = 0, double fh = 48,
                                  double fv = 36) const;
  std::pair<Descriptor, bool> descriptor(unsigned id, unsigned w, unsigned h, double yaw = 0,
                                         double pitch = 0, double roll = 0, double fh = 48,
                                         double fv = 36) const;
  V ray(unsigned id, unsigned cell, unsigned m = 32) const;
};
std::array<int64_t, 3> coordinates(const Descriptor &d, unsigned cell);
bool covered(const Descriptor &d, const std::array<int64_t, 3> &q);
unsigned project_reference(const Words &texture, Words &scene, const Descriptor &d);
} // namespace boayo
