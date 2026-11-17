// Regenerates the fixed table using the frozen v1 algorithm, not the optimized code.
#include "geometry_baseline.hpp"
#include <cstdio>
int main() {
  boayo::BaselineGeometry g;
  puts("// Generated from tests/geometry_baseline.hpp; hexadecimal literals preserve doubles.");
  auto emit = [](const char *name, const auto &items) {
    printf("static const std::array<Triangle, %zu> %s = {{\n", items.size(), name);
    for (const auto &t : items) {
      printf("Triangle{{");
      for (const auto &v : t) printf("V{%a,%a,%a},", v[0], v[1], v[2]);
      puts("}},");
    }
    puts("}};");
  };
  emit("fixed_faces", g.faces);
  emit("fixed_triangles", g.triangles);
  puts("static const std::array<int32_t, 180> fixed_camera = {{");
  for (auto v : g.camera()) printf("%d,", v);
  puts("}};");
}
