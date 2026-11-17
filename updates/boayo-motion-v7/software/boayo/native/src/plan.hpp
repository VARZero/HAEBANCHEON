#pragma once
#include "geometry.hpp"
#include "texture.hpp"
namespace boayo {
struct ProjectionPlan {
  std::shared_ptr<const Texture> storage;
  // Immutable device-ready queues and directory, generated on the build host.
  std::shared_ptr<const Texture> ready;
  uint32_t payload_crc=0;
  std::vector<unsigned> ids,kinds;
  std::vector<Descriptor> descriptors;
  std::vector<const uint8_t *> masks;
  std::vector<std::array<uint32_t,32>> packed_masks;
};
Words make_device_plan(const ProjectionPlan &);
std::string projection_plan_key(const J &geometry,const J &shape);
std::shared_ptr<const ProjectionPlan> load_projection_plan(const J &geometry,const J &shape);
J projection_plan_stats();
}
