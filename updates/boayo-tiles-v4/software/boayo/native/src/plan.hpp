#pragma once
#include "geometry.hpp"
#include "texture.hpp"
namespace boayo {
struct ProjectionPlan {
  std::shared_ptr<const Texture> storage;
  std::vector<unsigned> ids,kinds;
  std::vector<Descriptor> descriptors;
  std::vector<const uint8_t *> masks;
  std::vector<std::array<uint32_t,32>> packed_masks;
};
std::string projection_plan_key(const J &geometry,const J &shape);
std::shared_ptr<const ProjectionPlan> load_projection_plan(const J &geometry,const J &shape);
J projection_plan_stats();
}
