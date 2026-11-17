#define coordinates baseline_coordinates
#define covered baseline_covered
#define project_reference baseline_project_reference
#include "geometry_baseline.hpp"
#undef coordinates
#undef covered
#undef project_reference
#include "../src/geometry.hpp"
#include <cstring>
#include <iostream>
int main() {
  boayo::BaselineGeometry baseline;
  boayo::Geometry fixed;
  boayo::require(std::memcmp(baseline.faces.data(), fixed.faces.data(), sizeof(baseline.faces)) == 0,
                 "fixed face bits changed");
  boayo::require(std::memcmp(baseline.triangles.data(), fixed.triangles.data(), sizeof(baseline.triangles)) == 0,
                 "fixed triangle bits changed");
  unsigned count = 0;
  for (int view = 0; view < 8; ++view) {
    double yaw = view * 43.0, pitch = view * 13.0 - 40, roll = view * 11.0;
    boayo::require(baseline.camera(yaw,pitch,roll) == fixed.camera(yaw,pitch,roll), "camera changed");
    boayo::Geometry::Projection context(640,360,yaw,pitch,roll,48,36);
    for (unsigned tile = 0; tile < boayo::tiles; ++tile) {
      boayo::require(baseline.descriptor(tile,640,360,yaw,pitch,roll) == fixed.descriptor(tile,context),
                     "descriptor changed");
      ++count;
    }
  }
  boayo::require(baseline.camera() == fixed.camera(), "default camera changed");
  std::cout << "{\"result\":\"PASS\",\"fixed_geometry_bit_exact\":true,\"descriptors\":" << count << "}\n";
}
