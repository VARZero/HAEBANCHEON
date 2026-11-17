#pragma once
#include "../vendor/json.hpp"
#include <array>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <fstream>
namespace boayo {
using J = nlohmann::json;
using Bytes = std::vector<uint8_t>;
using Words = std::vector<uint32_t>;
using V = std::array<double, 3>;
constexpr double pi = 3.14159265358979323846;
constexpr unsigned tiles = 4220, header = 4476, tile_words = 256,
                   scene_words = (header + tiles * tile_words + 15) & ~15u;
inline double rad(double x) {
  return x * pi / 180;
}
inline void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
// A successful check must not allocate an error string on the target CPU.
inline void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
inline double finite(double x) {
  require(std::isfinite(x), "non-finite number");
  return x;
}
inline double azimuth(double x) {
  x = std::fmod(finite(x) + 180, 360);
  return (x < 0 ? x + 360 : x) - 180;
}
inline double elevation(double x) {
  return std::clamp(finite(x), -89.5, 89.5);
}
inline V operator+(V a, V b) {
  for (int i = 0; i < 3; i++)
    a[i] += b[i];
  return a;
}
inline V operator-(V a, V b) {
  for (int i = 0; i < 3; i++)
    a[i] -= b[i];
  return a;
}
inline V operator*(V a, double b) {
  for (auto &v : a)
    v *= b;
  return a;
}
inline double dot(V a, V b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline V cross(V a, V b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline V unit(V a) {
  return a * (1 / std::sqrt(dot(a, a)));
}
inline std::array<V, 3> axes(double yaw, double pitch, double roll = 0) {
  double a = rad(yaw), e = rad(pitch), l = rad(roll);
  V c = {std::cos(e) * std::sin(a), std::sin(e), -std::cos(e) * std::cos(a)};
  V r = {std::cos(a), 0, std::sin(a)}, u = cross(r, c);
  return {c, r * std::cos(l) + u * std::sin(l), u * std::cos(l) - r * std::sin(l)};
}
inline uint8_t rgb_index(unsigned r, unsigned g, unsigned b) {
  return (r & 224) | ((g >> 3) & 28) | (b >> 6);
}
inline uint32_t palette(unsigned k) {
  unsigned r = (k >> 5) * 255 / 7, g = ((k >> 2) & 7) * 255 / 7, b = (k & 3) * 255 / 3;
  if (((k >> 2) & 7) == (k >> 5) && (k & 3) == (r >> 6))
    g = b = r;
  return (r << 16) | (b << 8) | g;
}
inline Bytes unbase64(const std::string &s) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  require(s.size() % 4 == 0, "invalid base64 length");
  Bytes out;
  for (size_t i = 0; i < s.size(); i += 4) {
    uint32_t v = 0;
    unsigned pad = 0;
    for (unsigned k = 0; k < 4; k++) {
      if (s[i + k] == '=') {
        require(i + 4 == s.size() && k >= 2, "invalid base64 padding");
        ++pad;
        v <<= 6;
      } else {
        auto n = alphabet.find(s[i + k]);
        require(n != std::string::npos && !pad, "invalid base64");
        v = (v << 6) | unsigned(n);
      }
    }
    out.push_back(v >> 16);
    if (pad < 2)
      out.push_back(v >> 8);
    if (!pad)
      out.push_back(v);
  }
  return out;
}
inline std::string base64(const Bytes &v) {
  const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string s;
  for (size_t i = 0; i < v.size(); i += 3) {
    uint32_t n = uint32_t(v[i]) << 16;
    if (i + 1 < v.size())
      n |= uint32_t(v[i + 1]) << 8;
    if (i + 2 < v.size())
      n |= v[i + 2];
    s += a[n >> 18];
    s += a[(n >> 12) & 63];
    s += i + 1 < v.size() ? a[(n >> 6) & 63] : '=';
    s += i + 2 < v.size() ? a[n & 63] : '=';
  }
  return s;
}
inline void write_bytes(const std::string &path, const void *data, size_t size) {
  std::ofstream f(path, std::ios::binary);
  require(bool(f), "cannot open " + path);
  f.write(static_cast<const char *>(data), size);
  require(bool(f), "cannot write " + path);
}
} // namespace boayo
