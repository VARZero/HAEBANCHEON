std::pair<Descriptor, bool> Geometry::descriptor_reference(unsigned id, const Projection &p) const {
  require(id < tiles, "invalid tile");
  const auto &ax = p.ax;
  unsigned w = p.w, h = p.h;
  double tx = p.tx, ty = p.ty;
  std::array<V, 3> value;
  const auto &q = triangles[id];
  for (unsigned j = 0; j < 3; j++) {
    double d = dot(q[j], ax[0]);
    value[0][j] = (dot(q[j], ax[1]) / tx + d / 2) * (w - 1);
    value[1][j] = (-dot(q[j], ax[2]) / ty + d / 2) * (h - 1);
    value[2][j] = d;
  }
  double coeff[3][4], bound = 0;
  for (int j = 0; j < 3; j++) {
    auto v = value[j];
    coeff[j][0] = 94 * v[0] + v[1] + v[2];
    coeff[j][1] = 3 * (v[1] - v[0]);
    coeff[j][2] = 3 * (v[2] - v[1]);
    coeff[j][3] = v[0] - 2 * v[1] + v[2];
    bound = std::max(bound, std::abs(coeff[j][0]) +
                                31 * (std::abs(coeff[j][1]) + std::abs(coeff[j][2])) +
                                std::abs(coeff[j][3]));
  }
  double scale =
      std::exp2(std::min(22., std::floor(std::log2(std::exp2(29) / std::max(bound, 1e-30)))));
  Descriptor d{};
  d[1] = id * 256;
  d[2] = w;
  d[3] = h;
  d[4] = w;
  d[5] = 1;
  int64_t planes[5][4];
  for (int j = 0; j < 3; j++)
    for (int k = 0; k < 4; k++)
      d[6 + j * 4 + k] = uint32_t(int32_t(std::nearbyint(coeff[j][k] * scale)));
  for (int k = 0; k < 4; k++) {
    auto u = int32_t(d[6 + k]), v = int32_t(d[10 + k]), z = int32_t(d[14 + k]);
    planes[0][k] = z;
    planes[1][k] = u;
    planes[2][k] = v;
    planes[3][k] = int64_t(z) * (w - 1) - u;
    planes[4][k] = int64_t(z) * (h - 1) - v;
  }
  bool visible = true;
  for (int j = 0; j < 5; j++) {
    auto *p = planes[j];
    int64_t upper =
        std::max({p[0], p[0] + 31 * p[1], p[0] + 31 * (p[1] + p[2])}) + std::max(int64_t(0), p[3]);
    visible &= j == 0 ? upper > 0 : upper >= 0;
  }
  return {d, visible};
}