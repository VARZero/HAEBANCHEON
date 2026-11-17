#pragma once
#include "wm.hpp"
#include "ui.hpp"
#include <memory>
namespace boayo {
void publish(WM &wm, int id, const Surface &s, bool first = false);
std::pair<double, double> gaze(double az, double el, double w, double h, double u, double v);
J storage_layout(const J &windows, double el = -42, double w = 16, double h = 5);
struct App {
  WM &wm;
  std::string owner, kind, title;
  int id = 0, width = 400, height = 300;
  double az = 0, el = 0, wd = 38, hd = 28;
  bool focused = true, closed = false, minimized = false;
  double transition = -1;
  J restore_geometry, drag, last_pose;
  std::string hovered, dragging, pressed_zone;
  int keyboard_index = -1, clicks = 0, step = 0;
  uint64_t cancel_serial = 0;
  std::map<std::string, int> captions;
  std::map<std::string, J> caption_geometry;
  std::map<std::string, bool> caption_appearance;
  J published_shape, last_content_key;
  std::unique_ptr<Surface> content;
  double last_update = -1;
  bool published = false;
  App(WM &, std::string owner, std::string kind, double az = 0, double el = 0);
  J call(const std::string &op, J args = J::object());
  J geometry() const;
  J state() const;
  J shape() const;
  void sync_shape();
  void caption_sync();
  void metadata(std::string phase = "open", J extra = J::object());
  void redraw();
  void present(const J &scene_state);
  void cancel_drag();
  bool apply_drag(double yaw, double pitch);
  void finish_drag(double now, double yaw, double pitch, bool has_pose = true,
                   bool cancelled = false);
  void minimize(double now, J normal = nullptr);
  void restore(J position = J::object());
  void close();
  void tick(double now);
  void handle(J event, double now);
};
struct Workspace {
  WM &wm;
  std::string owner = "boayo-desktop";
  std::map<int, int> cards;
  std::map<int, J> targets, card_keys;
  std::map<std::string, int> feedback;
  std::map<std::string, J> feedback_keys;
  J drag;
  uint64_t cancel_serial = 0;
  int hovered = 0;
  explicit Workspace(WM &w) : wm(w) {}
  J call(const std::string &op, J args = J::object());
  int create(const std::string &, double, double, double, double, const std::string &, bool = false,
             int = 96);
  void show_feedback(const std::string &, double, double, double, double, const std::string &,
                     Color);
  void hide_feedback(const std::set<std::string> &keep = {});
  void sync();
  bool handle(const J &);
  void move(double, double);
  void finish(double, double);
  void cancel();
};
struct Desktop {
  WM &wm;
  Workspace workspace;
  int launcher = 0, hovered = -1, selected = -1, offset = 0, pressed = -1;
  bool mapped = true;
  J apps;
  std::map<int, std::unique_ptr<App>> running;
  std::string last_launch;
  uint64_t last_press_serial = 0;
  explicit Desktop(WM &w);
  int launch(std::string kind, double az = 0, double el = 0);
  void hide();
  void show(double az, double el);
  void update(double now);
  J action(const J &, double now);
};
} // namespace boayo
