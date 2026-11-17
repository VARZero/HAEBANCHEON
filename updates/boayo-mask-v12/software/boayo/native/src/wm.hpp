#pragma once
#include "common.hpp"
#include <functional>
#include <memory>
#include "texture.hpp"
#include "dirty_regions.hpp"
namespace boayo {
struct Window {
  J data;
  mutable Bytes surface;
  void ensure_surface() const;
  std::shared_ptr<const Texture> prepared;
  uint64_t revision = 1;
  uint64_t solid_revision=0;
  unsigned solid_index=0;
  std::array<int, 4> dirty{};
  std::vector<Rect> dirty_regions;
  uint64_t dirty_regions_revision=0;
};
struct Hit {
  int id = 0;
  double u = 0, v = 0;
  std::string zone = "content";
};
class WM {
public:
  std::map<int, Window> windows;
  std::vector<int> order;
  std::map<std::string, std::deque<J>> events;
  std::map<std::string, std::set<std::string>> input_sources;
  int next_id = 1, focus = 0, capture = 0, drag = 0;
  uint64_t generation = 1, press_serial = 0, release_serial = 0, cancel_serial = 0,
           scroll_serial = 0;
  double paz = 0, pel = 0, scroll_delta = 0, view_yaw = 0, view_pitch = 0, view_roll = 0;
  bool pointer_visible = false;
  std::set<std::string> buttons;
  J last_press = nullptr, last_release = nullptr;
  std::function<void(double, double, double)> pose;
  Window &window(int id, const std::string *owner = nullptr);
  J public_window(int id) const;
  void event(const std::string &owner, const std::string &type, J payload);
  void set_focus(int id);
  void focus_visible();
  J focus_window(int id, bool raise = true);
  void raise(int id);
  void destroy(const std::string &owner, int id);
  void disconnect(const std::string &owner);
  Hit hit(double az, double el, bool captured = false) const;
  J pointer_state(const Hit *hit = nullptr) const;
  J warp(double az, double el);
  J button(const std::string &name, bool pressed);
  J input_button(const std::string &owner, const std::string &name, bool pressed);
  J cancel();
  J state() const;
  J dispatch(const J &request);
};
J shape_validate(const J &shape, int width, int height);
bool shape_contains(const J &shape, double x, double y);
} // namespace boayo
