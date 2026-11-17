#include "apps.hpp"
#include "init_trace.hpp"
namespace boayo {
void publish(WM &wm, int id, const Surface &s, bool first) {
  init_trace::mark("surface.publish.begin", s.pixels.size());
  auto &w = wm.window(id);
  s.ensure_pixels();w.ensure_surface();
  require(w.surface.size() == s.pixels.size(), "surface size mismatch");
  int x0 = s.width, y0 = s.height, x1 = -1, y1 = -1;
  if(first) {x0=0;y0=0;x1=s.width-1;y1=s.height-1;}
  for (int y = 0; !first && y < s.height; y++) {
    if (init_trace::active && (y == 0 || y == s.height/2 || y == s.height-1)) init_trace::mark("surface.scan.row", y);
    for (int x = 0; x < s.width; x++) {
      size_t p = (size_t(y) * s.width + x) * 3;
      if (first ||
          !std::equal(s.pixels.begin() + p, s.pixels.begin() + p + 3, w.surface.begin() + p)) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  init_trace::mark("surface.scan.end", s.height);
  if (x1 < 0)
    return;
  init_trace::mark("surface.copy.begin", s.pixels.size());
  w.surface = s.pixels;
  w.prepared = s.prepared;
  init_trace::mark("surface.copy.end", s.pixels.size());
  if (w.dirty[2]) {
    x0 = std::min(x0, w.dirty[0]);
    y0 = std::min(y0, w.dirty[1]);
    x1 = std::max(x1, w.dirty[0] + w.dirty[2] - 1);
    y1 = std::max(y1, w.dirty[1] + w.dirty[3] - 1);
  }
  w.dirty = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
  ++w.revision;
  ++wm.generation;
  init_trace::mark("surface.publish.end", id);
}
void publish(WM &wm,int id,Surface &&s,bool first) {
  if(!first && !s.prepared){publish(wm,id,static_cast<const Surface &>(s),false);return;}
  auto &w=wm.window(id);
  require(w.data["surface_width"]==s.width && w.data["surface_height"]==s.height,"surface size mismatch");
  if(!first && w.prepared==s.prepared)return;
  // Immutable UI states replace the surface without a pixel comparison/copy.
  // Mark the full window dirty; hardware reprojects it in the existing order.
  w.surface=std::move(s.pixels);w.prepared=std::move(s.prepared);
  w.dirty={0,0,s.width,s.height};++w.revision;++wm.generation;
  init_trace::mark("surface.publish.move",id);
}
std::pair<double, double> gaze(double az, double el, double w, double h, double u, double v) {
  auto a = axes(az, el);
  V ray = unit(a[0] + a[1] * ((2 * u - 1) * std::tan(rad(w / 2))) +
               a[2] * ((1 - 2 * v) * std::tan(rad(h / 2))));
  return {std::atan2(ray[0], -ray[2]) * 180 / pi, std::asin(ray[1]) * 180 / pi};
}
J storage_layout(const J &windows, double el, double w, double h) {
  std::vector<J> sorted;
  for (auto &v : windows)
    sorted.push_back(v);
  std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) {
    return a["window_id"].template get<int>() < b["window_id"].template get<int>();
  });
  std::vector<std::array<double, 3>> placed;
  J result = J::object();
  for (auto &v : sorted) {
    double az = v["workspace"].value("dock_azimuth", v["azimuth"].get<double>()), candidate = 0,
           e = 0, span = 0;
    int row = 0;
    bool found = false;
    for (int offset = 0; offset < 360 && !found; offset++) {
      candidate = azimuth(az + offset * 2);
      for (row = 0; row < 5; row++) {
        e = el - row * (h + 1);
        double den = std::cos(rad(e)) - std::tan(rad(h / 2)) * std::abs(std::sin(rad(e)));
        span = std::atan(std::tan(rad(w / 2)) / std::max(.01, den)) * 180 / pi;
        bool clash = false;
        for (auto &p : placed)
          if (p[1] == row && std::abs(azimuth(p[0] - candidate)) < span + p[2] + 2)
            clash = true;
        if (!clash) {
          found = true;
          break;
        }
      }
    }
    require(found, "storage ring is full");
    placed.push_back({candidate, double(row), span});
    result[std::to_string(v["window_id"].get<int>())] = {candidate, e};
  }
  return result;
}
J App::call(const std::string &op, J args) {
  return wm.dispatch({{"app", owner}, {"op", op}, {"args", args}});
}
App::App(WM &w, std::string o, std::string k, double a, double e)
    : wm(w), owner(std::move(o)), kind(std::move(k)), az(a), el(e) {
  title = kind == "dashboard" ? "Dashboard" : kind == "telemetry" ? "Telemetry" : "SDK Pulse";
  if (kind == "dashboard") {
    width = 640;
    height = 440;
  }
  if (kind == "telemetry") {
    wd = 36;
    hd = 26;
  }
  auto result = call("create_window", {{"title", title},
                                       {"azimuth", az},
                                       {"elevation", el},
                                       {"width_deg", wd},
                                       {"height_deg", hd},
                                       {"surface_width", width},
                                       {"surface_height", height},
                                       {"decorated", false},
                                       {"workspace", {{"role", "app"}, {"state", "open"}}},
                                       {"shape", shape()}});
  id = result["window_id"];
  focused = result["focused"];
  sync_shape();
}
J App::geometry() const {
  return {{"azimuth", az}, {"elevation", el}, {"width_deg", wd}, {"height_deg", hd}};
}
J App::state() const {
  return {{"window_id", id},
          {"azimuth", az},
          {"elevation", el},
          {"width_deg", wd},
          {"height_deg", hd},
          {"surface_width", width},
          {"surface_height", height},
          {"content_width", width - 100},
          {"content_height", height - 82},
          {"focused", focused},
          {"closed", closed},
          {"minimized", minimized},
          {"interaction", transition >= 0
                              ? "minimizing"
                              : (!drag.is_null() ? drag["zone"].get<std::string>() : "idle")}};
}
J App::shape() const {
  return J::array({J::array({48, 4, width - 96, height - 12, 14})});
}
void App::sync_shape() {
  auto s = shape();
  if (s != published_shape) {
    call("configure_window", {{"window_id", id}, {"shape", s}});
    published_shape = s;
  }
}
void App::metadata(std::string phase, J extra) {
  J m = {{"role", "app"},
         {"state", phase},
         {"restore", restore_geometry.is_null() ? geometry() : restore_geometry}};
  m.update(extra);
  call("configure_window", {{"window_id", id}, {"workspace", m}});
}
void App::caption_sync() {
  bool visible = focused && !closed && !minimized && transition < 0;
  if (!visible) {
    for (auto &[name, wid] : captions)
      if (!caption_geometry[name].is_null()) {
        call("configure_window", {{"window_id", wid}, {"mapped", false}});
        caption_geometry[name] = nullptr;
      }
    return;
  }
  double hw = std::tan(rad(wd / 2)), hh = std::tan(rad(hd / 2));
  double right = std::atan((2. * (width - 48) / width - 1) * hw),
         left = std::atan((96. / width - 1) * hw), gap = rad(2.6);
  double ru = (std::tan(right + gap) / hw + 1) / 2, lu = (std::tan(left - gap) / hw + 1) / 2;
  double top = std::max(rad(2.3), std::atan((1 - 8. / height) * hh) - rad(2)),
         tv = (1 - std::tan(top) / hh) / 2;
  for (std::string name : {"move", "resize_right", "close"}) {
    double u = name == "move" ? lu : ru, v = name == "move" ? .5 : name == "close" ? tv : 1 - tv;
    auto [a, e] = gaze(az, el, wd, hd, u, v);
    auto parent = axes(az, el), local = axes(a, e);
    V normal = parent[2] * dot(local[0], parent[0]) - parent[0] * dot(local[0], parent[2]);
    V aligned = cross(local[0], normal);
    double roll = std::atan2(dot(aligned, local[2]), dot(aligned, local[1])) * 180 / pi;
    J position = {a, e, roll};
    int h = name == "move" ? 100 : 40;
    if (!captions.count(name)) {
      auto info = call("create_window",
                       {{"title", name + ": " + title},
                        {"azimuth", a},
                        {"elevation", e},
                        {"width_deg", name == "move" ? 2.88 : 4.},
                        {"height_deg", name == "move" ? 10. : 4.},
                        {"surface_width", 40},
                        {"surface_height", h},
                        {"decorated", false},
                        {"always_on_top", true},
                        {"focus_on_create", false},
                        {"workspace", {{"role", "caption"}, {"parent", id}, {"control", name}}},
                        {"shape", J::array({J::array({0, 0, 40, h, 8})})}});
      int wid = info["window_id"];
      call("configure_window", {{"window_id", wid}, {"roll", roll}});
      captions[name] = wid;
      caption_geometry[name] = position;
    } else if (caption_geometry[name] != position) {
      call("configure_window", {{"window_id", captions[name]},
                                {"mapped", true},
                                {"azimuth", a},
                                {"elevation", e},
                                {"roll", roll}});
      caption_geometry[name] = position;
    }
    bool selected = name == hovered || name == dragging;
    if (!caption_appearance.count(name) || caption_appearance[name] != selected) {
      publish(wm, captions[name], caption_image(name, selected, h),
              !caption_appearance.count(name));
      caption_appearance[name] = selected;
    }
  }
}
void App::redraw() {
  if (!closed) {
    sync_shape();
    caption_sync();
  }
  if (!content || minimized || closed)
    return;
  auto image = app_frame(title, width, height, focused, hovered, true);
  image.mutate();content->ensure_pixels();
  for (int y = 48; y < height - 34; y++)
    std::copy_n(content->pixels.data() + size_t(y - 48) * (width - 100) * 3, (width - 100) * 3,
                image.pixels.data() + (size_t(y) * width + 50) * 3);
  publish(wm, id, image, !published);
  published = true;
}
void App::present(const J &scene) {
  if (closed)
    return;
  int n = 0, stored = 0;
  for (auto &w : scene.at("windows"))
    if (w["workspace"].value("role", std::string()) == "app") {
      ++n;
      stored += w["workspace"].value("state", std::string()) == "minimized";
    }
  J s = {{"width", width}, {"height", height}, {"focused", focused}, {"hovered", hovered},
         {"step", step},   {"clicks", clicks}, {"apps", n},          {"stored", stored},
         {"yaw", 0.},      {"pitch", 0.},      {"width_deg", wd},    {"height_deg", hd}};
  if (s == last_content_key)
    return;
  auto image = app_image(kind, s);
  content = std::make_unique<Surface>(width - 100, height - 82);
  for (int y = 0; y < height - 82; y++)
    std::copy_n(image.pixels.data() + (size_t(y + 48) * width + 50) * 3, (width - 100) * 3,
                content->pixels.data() + size_t(y) * (width - 100) * 3);
  if (!minimized && transition < 0) {
    publish(wm, id, image, !published);
    published = true;
    caption_sync();
  }
  last_content_key = s;
}
void App::cancel_drag() {
  bool held = !drag.is_null();
  drag = nullptr;
  last_pose = nullptr;
  pressed_zone.clear();
  dragging.clear();
  if (held)
    call("release_pointer");
}
bool App::apply_drag(double yaw, double pitch) {
  if (drag.is_null())
    return false;
  if (!last_pose.is_null() && std::abs(yaw - last_pose[0].get<double>()) < .01 &&
      std::abs(pitch - last_pose[1].get<double>()) < .01)
    return false;
  last_pose = {yaw, pitch};
  std::string zone = drag["zone"];
  double da = azimuth(yaw - drag["start_az"].get<double>()),
         de = pitch - drag["start_el"].get<double>(), a = drag["az"], e = drag["el"],
         w = drag["wd"], h = drag["hd"];
  if (zone == "move") {
    az = azimuth(a + da);
    el = elevation(e + de);
    call("configure_window", {{"window_id", id}, {"azimuth", az}, {"elevation", el}});
  } else {
    double factor = zone == "resize_left" ? -1 : 1;
    wd = std::clamp(w + factor * da, 10., 100.);
    hd = std::clamp(h - de, 8., 80.);
    az = azimuth(a + factor * (wd - w) / 2);
    el = elevation(e - (hd - h) / 2);
    auto g = geometry();
    g["window_id"] = id;
    call("configure_window", g);
  }
  metadata(zone == "move" ? "moving" : "resizing",
           {{"drop_candidate", zone == "move" && pitch <= -36}});
  caption_sync();
  return true;
}
void App::minimize(double now, J normal) {
  if (closed || minimized || transition >= 0)
    return;
  restore_geometry = normal.is_null() ? geometry() : normal;
  cancel_drag();
  transition = now;
  metadata("minimizing", {{"dock_azimuth", az}});
}
void App::restore(J position) {
  if (closed)
    return;
  J g = restore_geometry.is_null() ? geometry() : restore_geometry;
  if (position.contains("azimuth") && !position["azimuth"].is_null() &&
      position.contains("elevation") && !position["elevation"].is_null()) {
    g["azimuth"] = azimuth(position["azimuth"]);
    g["elevation"] = std::clamp(position["elevation"].get<double>(), -85., 85.);
  }
  az = g["azimuth"];
  el = g["elevation"];
  wd = g["width_deg"];
  hd = g["height_deg"];
  minimized = false;
  transition = -1;
  g["window_id"] = id;
  g["mapped"] = true;
  call("configure_window", g);
  call("focus_window", {{"window_id", id}});
  focused = true;
  metadata();
  redraw();
}
void App::close() {
  if (closed)
    return;
  cancel_drag();
  for (auto &[name, wid] : captions)
    call("destroy_window", {{"window_id", wid}});
  captions.clear();
  call("destroy_window", {{"window_id", id}});
  closed = true;
}
void App::tick(double now) {
  caption_sync();
  if (transition >= 0 && now - transition >= .22) {
    transition = -1;
    minimized = true;
    focused = false;
    call("configure_window", {{"window_id", id}, {"mapped", false}});
    metadata("minimized", {{"dock_azimuth", az}});
  }
}
void App::finish_drag(double now, double yaw, double pitch, bool has_pose, bool cancelled) {
  if (drag.is_null())
    return;
  J original = drag;
  std::string zone = drag["zone"];
  if (!cancelled && has_pose)
    apply_drag(yaw, pitch);
  if (!cancelled && zone == "move" && has_pose && pitch <= -36)
    minimize(now, {{"azimuth", original["az"]},
                   {"elevation", original["el"]},
                   {"width_deg", original["wd"]},
                   {"height_deg", original["hd"]}});
  else {
    cancel_drag();
    metadata();
  }
  redraw();
}
void App::handle(J ev, double now) {
  int source = ev.value("window_id", 0);
  std::string caption;
  for (auto &[name, wid] : captions)
    if (wid == source) {
      caption = name;
      ev["window_id"] = id;
      break;
    }
  if (ev.value("window_id", 0) != id || closed)
    return;
  std::string type = ev.value("type", std::string());
  bool inside = ev.value("u", -1.) >= 0 && ev.value("u", -1.) <= 1 && ev.value("v", -1.) >= 0 &&
                ev.value("v", -1.) <= 1;
  std::string zone;
  if (!caption.empty()) {
    if (inside)
      zone = caption;
  } else {
    double x = ev.value("u", -1.) * width, y = ev.value("v", -1.) * height;
    if (x >= 50 && x < width - 50 && y >= 48 && y < height - 34)
      zone = "content";
    if (focused) {
      for (auto name : {"move", "resize_right", "close"}) {
        double cx = std::string(name) == "move" ? 24 : width - 24,
               cy = std::string(name) == "move"    ? height / 2
                    : std::string(name) == "close" ? 26
                                                   : height - 26;
        if (x >= cx - 25 && x < cx + 25 && y >= cy - 25 && y < cy + 25) {
          zone = name;
          break;
        }
      }
    }
  }
  if (type == "focus") {
    focused = wm.focus == id;
    for (auto &v : captions)
      focused |= wm.focus == v.second;
    if (captions.empty())
      focused = ev.value("focused", false);
    if (!focused) {
      cancel_drag();
      hovered.clear();
    }
    redraw();
    return;
  }
  if (type == "pointer_leave") {
    if (pressed_zone == "close")
      pressed_zone.clear();
    hovered.clear();
    redraw();
    return;
  }
  if (type == "window_action") {
    std::string action = ev.value("action", std::string());
    if (action == "minimize")
      minimize(now);
    else if (action == "restore")
      restore(ev);
    else if (action == "close")
      close();
    return;
  }
  if (type == "pointer_cancel") {
    finish_drag(now, 0, 0, false, true);
    pressed_zone.clear();
    return;
  }
  if (type == "key" && ev.value("pressed", false)) {
    std::string key = ev.value("key", std::string());
    if (key == "tab" && focused) {
      keyboard_index = (keyboard_index + 1) % 3;
      hovered = std::array<std::string, 3>{"move", "resize_right", "close"}[keyboard_index];
      redraw();
    } else if (key == "escape") {
      finish_drag(now, 0, 0, false, true);
      pressed_zone.clear();
    }
    return;
  }
  if (minimized || transition >= 0)
    return;
  if (type == "pointer_button" && ev.value("button", std::string()) == "left") {
    if (ev.value("pressed", false)) {
      cancel_serial = ev.value("cancel_serial", cancel_serial);
      pressed_zone = zone;
      if (zone == "move" || zone == "resize_left" || zone == "resize_right") {
        auto [starta, starte] = gaze(az, el, wd, hd, ev.at("u"), ev.at("v"));
        if (ev.contains("azimuth") && ev.contains("elevation")) {
          starta = ev["azimuth"];
          starte = ev["elevation"];
        }
        drag = {{"zone", zone}, {"u", ev.at("u")},    {"v", ev.at("v")},
                {"az", az},     {"el", el},           {"wd", wd},
                {"hd", hd},     {"start_az", starta}, {"start_el", starte}};
        last_pose = nullptr;
        dragging = zone;
        call("capture_pointer", {{"window_id", caption.empty() ? id : source}});
        metadata(zone == "move" ? "moving" : "resizing");
        redraw();
      } else if (zone == "content" && kind == "sdk-pulse")
        ++clicks;
      return;
    }
    if (!drag.is_null())
      finish_drag(now, ev.value("azimuth", 0.), ev.value("elevation", 0.),
                  ev.contains("azimuth") && ev.contains("elevation"));
    else if (zone == "close" && pressed_zone == "close")
      close();
    pressed_zone.clear();
    return;
  }
  if (type == "pointer_motion") {
    if (drag.is_null()) {
      if (hovered != zone) {
        hovered = zone;
        redraw();
      }
    } else if (ev.contains("azimuth") && ev.contains("elevation"))
      apply_drag(ev["azimuth"], ev["elevation"]);
    else {
      double du = ev.at("u").get<double>() - drag["u"].get<double>(),
             dv = ev.at("v").get<double>() - drag["v"].get<double>();
      std::string z = drag["zone"];
      if (z == "move") {
        az = drag["az"].get<double>() + du * drag["wd"].get<double>();
        el = elevation(drag["el"].get<double>() - dv * drag["hd"].get<double>());
        call("configure_window", {{"window_id", id}, {"azimuth", az}, {"elevation", el}});
      } else {
        wd = std::clamp(drag["wd"].get<double>() +
                            (z == "resize_left" ? -1 : 1) * du * drag["wd"].get<double>(),
                        10., 100.);
        hd = std::clamp(drag["hd"].get<double>() + dv * drag["hd"].get<double>(), 8., 80.);
        call("configure_window", {{"window_id", id}, {"width_deg", wd}, {"height_deg", hd}});
      }
    }
  }
}
J Workspace::call(const std::string &op, J a) {
  return wm.dispatch({{"op", op}, {"app", owner}, {"args", a}});
}
int Workspace::create(const std::string &title, double az, double el, double w, double h,
                      const std::string &role, bool input, int pixels) {
  return call("create_window", {{"title", title},
                                {"azimuth", az},
                                {"elevation", el},
                                {"width_deg", w},
                                {"height_deg", h},
                                {"surface_width", 320},
                                {"surface_height", pixels},
                                {"always_on_top", true},
                                {"decorated", false},
                                {"input_enabled", input},
                                {"focus_on_create", false},
                                {"workspace", {{"role", role}}}})["window_id"];
}
void Workspace::show_feedback(const std::string &name, double az, double el, double w, double h,
                              const std::string &label, Color color) {
  auto round2 = [](double x) { return std::nearbyint(x * 100) / 100; };
  J key = J::array({round2(az), round2(el), round2(w), round2(h), label, color});
  if (!feedback.count(name))
    feedback[name] = create(label, az, el, w, h, "feedback", false, name == "preview" ? 240 : 96);
  if (feedback_keys[name] == key)
    return;
  call("configure_window", {{"window_id", feedback[name]},
                            {"azimuth", az},
                            {"elevation", el},
                            {"width_deg", w},
                            {"height_deg", h},
                            {"mapped", true}});
  publish(wm, feedback[name], feedback_image(name, label, color), !feedback_keys.count(name));
  feedback_keys[name] = key;
}
void Workspace::hide_feedback(const std::set<std::string> &keep) {
  for (auto &[name, id] : feedback)
    if (!keep.count(name) && !feedback_keys[name].is_null()) {
      call("configure_window", {{"window_id", id}, {"mapped", false}});
      feedback_keys[name] = nullptr;
    }
}
void Workspace::cancel() {
  bool held = !drag.is_null();
  drag = nullptr;
  if (held)
    call("release_pointer");
  hide_feedback();
}
void Workspace::move(double az, double el) {
  if (drag.is_null())
    return;
  double da = std::abs(azimuth(az - drag["az"].get<double>())), de = el - drag["el"].get<double>();
  drag["moved"] = drag["moved"].get<bool>() || std::hypot(da * std::cos(rad(drag["el"])), de) > 1;
  if (!drag["moved"].get<bool>())
    return;
  int target = drag["target"];
  if (!targets.count(target)) {
    cancel();
    return;
  }
  auto &t = targets[target];
  show_feedback("ghost", az, el, 14, 4, t["title"], focus_color);
  std::set<std::string> keep = {"ghost"};
  if (el > drag["el"].get<double>() + 5) {
    auto g = t["workspace"].value("restore", t);
    show_feedback("preview", az, std::max(-30., el), g["width_deg"], g["height_deg"], t["title"],
                  focus_color);
    keep.insert("preview");
  }
  if (el <= -76) {
    show_feedback("close", az, -76, 28, 5, "RELEASE TO CLOSE", danger);
    keep.insert("close");
  }
  hide_feedback(keep);
}
void Workspace::finish(double az, double el) {
  if (drag.is_null())
    return;
  move(az, el);
  if (drag.is_null())
    return;
  J a = {{"window_id", drag["target"]}};
  if (!drag["moved"].get<bool>())
    a["action"] = "restore";
  else if (el <= -76)
    a["action"] = "close";
  else if (el > drag["el"].get<double>() + 5) {
    a["action"] = "restore";
    a["azimuth"] = az;
    a["elevation"] = std::max(-30., el);
  }
  if (a.contains("action"))
    call("request_window_action", a);
  cancel();
}
bool Workspace::handle(const J &e) {
  int target = 0;
  for (auto &[t, id] : cards)
    if (id == e.value("window_id", 0)) {
      target = t;
      break;
    }
  if (!target)
    return false;
  std::string kind = e.value("type", std::string());
  if (kind == "pointer_cancel")
    cancel();
  else if (kind == "pointer_button" && e.value("button", std::string()) == "left") {
    if (!e.contains("azimuth") || !e.contains("elevation"))
      return true;
    if (e.value("pressed", false)) {
      drag = {{"target", target},
              {"az", e["azimuth"]},
              {"el", e["elevation"]},
              {"moved", false},
              {"cancel_serial", cancel_serial}};
      call("capture_pointer", {{"window_id", cards[target]}});
    } else if (!drag.is_null())
      finish(e["azimuth"], e["elevation"]);
  } else if (kind == "pointer_motion" && !drag.is_null())
    move(e["azimuth"], e["elevation"]);
  else if (kind == "key" && e.value("pressed", false)) {
    std::string key = e.value("key", std::string());
    if (key == "enter" || key == "space")
      call("request_window_action", {{"window_id", target}, {"action", "restore"}});
    else if (key == "escape")
      cancel();
  }
  return true;
}
void Workspace::sync() {
  auto state = wm.state();
  J minimized = J::array();
  targets.clear();
  for (auto &w : state["windows"])
    if (w["workspace"].value("role", std::string()) == "app") {
      targets[w["window_id"]] = w;
      if (w["workspace"].value("state", std::string()) == "minimized")
        minimized.push_back(w);
    }
  auto layout = storage_layout(minimized);
  for (auto i = cards.begin(); i != cards.end();)
    if (!layout.contains(std::to_string(i->first))) {
      if (!drag.is_null() && drag["target"] == i->first)
        cancel();
      call("destroy_window", {{"window_id", i->second}});
      card_keys.erase(i->first);
      i = cards.erase(i);
    } else
      ++i;
  auto pointer = state["pointer"];
  hovered = 0;
  for (auto &[target, card] : cards)
    if (pointer["window_id"] == card)
      hovered = target;
  for (auto &t : minimized) {
    int target = t["window_id"];
    auto pos = layout[std::to_string(target)];
    if (!cards.count(target))
      cards[target] = create(t["title"], pos[0], pos[1], 16, 5, "storage", true);
    bool dim = !drag.is_null() && drag["target"] == target && drag["moved"].get<bool>(),
         closing = dim && pointer["elevation"].get<double>() <= -76,
         selected = hovered == target || state["focused_window"] == cards[target];
    J key = J::array({pos[0], pos[1], t["title"], selected, dim, closing});
    if (card_keys[target] != key) {
      call("configure_window",
           {{"window_id", cards[target]}, {"azimuth", pos[0]}, {"elevation", pos[1]}});
      publish(wm, cards[target], storage_image(t["title"], selected, dim, closing));
      card_keys[target] = key;
    }
  }
  if (!drag.is_null()) {
    if (pointer["cancel_serial"] != cancel_serial)
      cancel();
    else if (wm.buttons.count("left"))
      move(wm.paz, wm.pel);
    else {
      auto final = pointer["last_left_release"].is_null() ? pointer : pointer["last_left_release"];
      finish(final["azimuth"], final["elevation"]);
    }
  }
  cancel_serial = wm.cancel_serial;
  if (drag.is_null()) {
    bool moving = false;
    for (auto &[id, t] : targets)
      if (t["workspace"].value("state", std::string()) == "moving") {
        show_feedback("store", wm.paz, -36, 30, 5, "RELEASE TO STORE", focus_color);
        hide_feedback({"store"});
        moving = true;
        break;
      }
    if (!moving)
      hide_feedback();
  }
}
Desktop::Desktop(WM &w) : wm(w), workspace(w) {
  init_trace::Scope trace;
  apps = J::array({{{"id", "dashboard"}, {"name", "Dashboard"}, {"color", "#347CFF"}},
                   {{"id", "telemetry"}, {"name", "Telemetry"}, {"color", "#33B57A"}},
                   {{"id", "sdk-pulse"}, {"name", "SDK Pulse"}, {"color", "#347CFF"}}});
  init_trace::mark("window.create.begin");
  launcher = workspace.call("create_window", {{"title", "BoAYO Launcher"},
                                              {"azimuth", 0},
                                              {"elevation", 0},
                                              {"width_deg", 42},
                                              {"height_deg", 30},
                                              {"surface_width", 640},
                                              {"surface_height", 360},
                                              {"always_on_top", true}})["window_id"];
  init_trace::mark("window.create.end", launcher);
  init_trace::mark("window.configure.begin");
  workspace.call(
      "configure_window",
      {{"window_id", launcher}, {"shape", J::array({J::array({51.2, 36., 537.6, 252., 18.})})}});
  init_trace::mark("window.configure.end", launcher);
  publish(wm, launcher, launcher_image(apps), true);
  init_trace::mark("desktop.end", launcher);
}
int Desktop::launch(std::string kind, double az, double el) {
  if (kind == "pulse")
    kind = "sdk-pulse";
  require(kind == "dashboard" || kind == "telemetry" || kind == "sdk-pulse", "unknown app");
  std::string owner = "native-" + kind + "-" + std::to_string(wm.next_id);
  try {
    auto app = std::make_unique<App>(wm, owner, kind, az, el);
    int id = app->id;
    app->present(wm.state());
    last_launch = app->title;
    running[id] = std::move(app);
    hide();
    return id;
  } catch (...) {
    wm.disconnect(owner);
    throw;
  }
}
void Desktop::hide() {
  if (mapped)
    workspace.call("configure_window", {{"window_id", launcher}, {"mapped", false}});
  mapped = false;
  hovered = selected = pressed = -1;
}
void Desktop::show(double az, double el) {
  workspace.call("configure_window",
                 {{"window_id", launcher}, {"mapped", true}, {"azimuth", az}, {"elevation", el}});
  workspace.call("focus_window", {{"window_id", launcher}});
  mapped = true;
  hovered = selected = pressed = -1;
  publish(wm, launcher, launcher_image(apps));
}
void Desktop::update(double now) {
  if (wm.press_serial != last_press_serial && mapped && !wm.last_press.is_null()) {
    auto &d = wm.windows.at(launcher).data;
    auto a = axes(d["azimuth"], d["elevation"]);
    V q = axes(wm.last_press["azimuth"], wm.last_press["elevation"])[0];
    double depth = dot(q, a[0]);
    bool inside = false;
    if (depth > 0) {
      double x = (dot(q, a[1]) / depth / std::tan(rad(21)) + 1) * 320,
             y = (1 - dot(q, a[2]) / depth / std::tan(rad(15))) * 180;
      inside = shape_contains(d["shape"], x, y);
    }
    if (!inside)
      hide();
  }
  last_press_serial = wm.press_serial;
  auto evs = workspace.call("poll_events");
  for (auto &e : evs) {
    if (workspace.handle(e))
      continue;
    if (!mapped || e.value("window_id", 0) != launcher)
      continue;
    std::string type = e.value("type", std::string());
    if (type != "pointer_motion" && type != "pointer_button")
      continue;
    double x = e.value("u", 0.) * 640, y = e.value("v", 0.) * 360;
    int before = hovered;
    hovered = -1;
    double left = 51.2 + 537.6 * .34 + 20, right = 51.2 + 537.6 - 18, top = 58;
    int row = int((y - top) / 54);
    if (x >= left && x <= right && y >= top && row < 3 && row + offset < int(apps.size()))
      hovered = row + offset;
    if (type == "pointer_button" && e.value("button", std::string()) == "left") {
      if (e.value("pressed", false)) {
        if (x < 51.2 || x > 588.8 || y < 36 || y > 288) {
          hide();
          continue;
        }
        selected = hovered;
        pressed = selected;
      } else if (pressed >= 0) {
        int app = pressed;
        pressed = -1;
        auto &d = wm.windows[launcher].data;
        launch(apps[app]["id"], d["azimuth"], d["elevation"]);
      }
    }
    if (mapped && (before != hovered || type == "pointer_button"))
      publish(wm, launcher, launcher_image(apps, selected, hovered, offset));
  }
  // Deliver app-owned events once per tick; events generated by handling them remain queued.
  for (auto &[id, a] : running)
    if (!a->closed) {
      auto q = a->call("poll_events");
      for (auto &e : q)
        a->handle(e, now);
      if (a->closed)
        continue;
      a->tick(now);
      double period = a->kind == "telemetry" ? 1. / 12 : a->kind == "sdk-pulse" ? 1. : .5;
      if (a->last_update < 0 || now - a->last_update >= period) {
        if (a->last_update >= 0) {
          if (a->kind == "telemetry")
            ++a->step;
          else if (a->kind == "sdk-pulse")
            a->step = (a->step + 1) % 10;
        }
        a->last_update = now;
        a->present(wm.state());
      } else if (a->kind == "sdk-pulse")
        a->present(wm.state());
    }
  workspace.sync();
  for (auto it = running.begin(); it != running.end();)
    if (it->second->closed) {
      wm.disconnect(it->second->owner);
      it = running.erase(it);
    } else
      ++it;
}
J Desktop::action(const J &a, double now) {
  std::string op = a.at("op");
  if (op == "launch")
    return launch(a.at("kind"), a.value("azimuth", 0.), a.value("elevation", 0.));
  if (op == "show_launcher") {
    show(a.value("azimuth", wm.view_yaw), a.value("elevation", wm.view_pitch));
    return true;
  }
  if (op == "tick") {
    update(now);
    return true;
  }
  int id = a.at("window_id");
  auto it = running.find(id);
  require(it != running.end(), "unknown app window");
  auto &app = *it->second;
  if (op == "state")
    return app.state();
  if (op == "minimize")
    app.minimize(now);
  else if (op == "restore")
    app.restore(a);
  else if (op == "close")
    app.close();
  else if (op == "event")
    app.handle(a.at("event"), now);
  else
    throw std::runtime_error("unknown app action");
  return app.state();
}
} // namespace boayo
