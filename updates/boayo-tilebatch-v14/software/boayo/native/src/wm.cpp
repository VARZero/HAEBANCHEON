#include "wm.hpp"
#include "rgb_fill.hpp"
namespace boayo {
void Window::ensure_surface() const {
  if(!surface.empty())return;
  if(prepared){materialize_rgb(surface,*prepared);return;}
  surface.resize(size_t(data["surface_width"].get<int>())*data["surface_height"].get<int>()*3);
  fill_rgb(surface.data(),surface.size(),{16,24,32});
}

static J nullable(int id) {
  return id ? J(id) : J(nullptr);
}
static J workspace_validate(const J &j) {
  require(j.is_object() && j.dump().size() <= 4096, "invalid workspace metadata");
  return j;
}
J shape_validate(const J &shape, int width, int height) {
  require(shape.is_array() && shape.size() <= 16,
          "shape must contain at most 16 rounded rectangles");
  J out = J::array();
  for (auto &r : shape) {
    require(r.is_array() && r.size() == 5, "shape rectangle requires x,y,width,height,radius");
    double x = finite(r[0]), y = finite(r[1]), w = finite(r[2]), h = finite(r[3]), v = finite(r[4]);
    require(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= width && y + h <= height && v >= 0 &&
                v <= std::min(w, h) / 2,
            "invalid shape rectangle");
    out.push_back({x, y, w, h, v});
  }
  return out;
}
bool shape_contains(const J &s, double x, double y) {
  if (s.empty())
    return true;
  for (auto &r : s) {
    double rx = r[0], ry = r[1], w = r[2], h = r[3], v = r[4],
           dx = std::max({rx + v - x, x - (rx + w - v), 0.}),
           dy = std::max({ry + v - y, y - (ry + h - v), 0.});
    if (x >= rx && x < rx + w && y >= ry && y < ry + h && dx * dx + dy * dy <= v * v)
      return true;
  }
  return false;
}
Window &WM::window(int id, const std::string *owner) {
  auto i = windows.find(id);
  require(i != windows.end(), "unknown window");
  require(!owner || i->second.data["owner"] == *owner, "window belongs to another application");
  return i->second;
}
J WM::public_window(int id) const {
  J j = windows.at(id).data;
  j["focused"] = id == focus;
  j["z"] = std::find(order.begin(), order.end(), id) - order.begin();
  return j;
}
void WM::event(const std::string &owner, const std::string &type, J p) {
  p["type"] = type;
  auto &q = events[owner];
  q.push_back(p);
  if (q.size() > 256)
    q.pop_front();
}
void WM::set_focus(int id) {
  if (id == focus)
    return;
  if (focus && windows.count(focus))
    event(windows.at(focus).data["owner"], "focus", {{"window_id", focus}, {"focused", false}});
  focus = id;
  if (id)
    event(windows.at(id).data["owner"], "focus", {{"window_id", id}, {"focused", true}});
}
void WM::focus_visible() {
  int id = 0;
  for (auto i = order.rbegin(); i != order.rend(); i++) {
    auto &w = windows.at(*i).data;
    if (w["mapped"].get<bool>() && w["input_enabled"].get<bool>()) {
      id = *i;
      break;
    }
  }
  set_focus(id);
}
void WM::raise(int id) {
  auto &w = window(id);
  order.erase(std::find(order.begin(), order.end(), id));
  if (w.data["always_on_top"].get<bool>())
    order.push_back(id);
  else {
    size_t n = std::count_if(order.begin(), order.end(),
                             [&](int k) { return windows[k].data["always_on_top"].get<bool>(); });
    order.insert(order.end() - n, id);
  }
}
J WM::focus_window(int id, bool up) {
  auto &w = window(id);
  require(w.data["mapped"].get<bool>() && w.data["input_enabled"].get<bool>(),
          "cannot focus an inactive window");
  set_focus(id);
  if (up)
    raise(id);
  ++generation;
  return public_window(id);
}
void WM::destroy(const std::string &owner, int id) {
  window(id, &owner);
  if (capture == id)
    capture = 0;
  if (drag == id)
    drag = 0;
  windows.erase(id);
  order.erase(std::find(order.begin(), order.end(), id));
  if (focus == id)
    focus_visible();
  ++generation;
}
void WM::disconnect(const std::string &owner) {
  auto held = input_sources[owner];
  input_sources.erase(owner);
  for (auto &b : held) {
    bool other = false;
    for (auto &v : input_sources)
      other |= v.second.count(b) != 0;
    if (!other)
      button(b, false);
  }
  auto ids = order;
  for (int id : ids)
    if (windows.at(id).data["owner"] == owner)
      destroy(owner, id);
  events.erase(owner);
}
static std::array<V, 3> float_axes(double a, double e, double r) {
  auto v = axes(a, e, r); // Match the Python WM's float32 basis arithmetic.
  auto c = axes(a, e, 0)[0], right = axes(a, e, 0)[1];
  for (auto &x : c)
    x = float(x);
  for (auto &x : right)
    x = float(x);
  V up = cross(right, c);
  for (auto &x : up)
    x = float(x);
  float co = std::cos(rad(r)), si = std::sin(rad(r));
  for (int k = 0; k < 3; k++) {
    v[0][k] = c[k];
    v[1][k] = float(float(right[k] * co) + float(up[k] * si));
    v[2][k] = float(float(up[k] * co) - float(right[k] * si));
  }
  return v;
}
Hit WM::hit(double az, double el, bool captured) const {
  V q = axes(azimuth(az), elevation(el))[0];
  for (auto &x : q)
    x = float(x);
  std::vector<int> ids;
  if (captured && capture && windows.count(capture))
    ids.push_back(capture);
  else
    ids.assign(order.rbegin(), order.rend());
  for (int id : ids) {
    auto &w = windows.at(id).data;
    bool forced = captured && capture == id;
    if (!forced && (!w["mapped"].get<bool>() || !w["input_enabled"].get<bool>()))
      continue;
    auto a = float_axes(w["azimuth"], w["elevation"], w["roll"]);
    auto fdot = [&](V v) {
      return float(float(float(q[0] * v[0]) + float(q[1] * v[1])) + float(q[2] * v[2]));
    };
    float d = fdot(a[0]), safe = std::max(d, 1e-8f);
    float x = float(fdot(a[1]) / safe) / float(std::tan(rad(w["width_deg"].get<double>() / 2))),
          y = float(fdot(a[2]) / safe) / float(std::tan(rad(w["height_deg"].get<double>() / 2)));
    double u = float((x + 1.f) * .5f), v = float((1.f - y) * .5f);
    if (forced)
      return {id, u, v, "content"};
    if (d > 0 && std::abs(x) <= 1 && std::abs(y) <= 1 &&
        shape_contains(w["shape"], u * (w["surface_width"].get<int>() - 1),
                       v * (w["surface_height"].get<int>() - 1)))
      return {id, u, v, w["decorated"].get<bool>() && v < .14 ? "title" : "content"};
  }
  return {};
}
J WM::pointer_state(const Hit *hp) const {
  Hit h = hp ? *hp : hit(paz, pel);
  return {{"azimuth", paz},
          {"elevation", pel},
          {"visible", pointer_visible},
          {"window_id", nullable(h.id)},
          {"zone", h.id ? J(h.zone) : J(nullptr)},
          {"dragging", bool(drag)},
          {"buttons", buttons},
          {"left_press_serial", press_serial},
          {"last_left_press", last_press},
          {"left_release_serial", release_serial},
          {"last_left_release", last_release},
          {"cancel_serial", cancel_serial},
          {"captured_window", nullable(capture)},
          {"scroll_serial", scroll_serial},
          {"scroll_delta", scroll_delta}};
}
J WM::warp(double az, double el) {
  Hit previous = hit(paz, pel, true);
  double oldaz = paz, oldel = pel;
  paz = azimuth(az);
  pel = elevation(el);
  if (drag && windows.count(drag)) {
    auto &w = windows[drag].data;
    w["azimuth"] = azimuth(w["azimuth"].get<double>() + azimuth(paz - oldaz));
    w["elevation"] = elevation(w["elevation"].get<double>() + pel - oldel);
  }
  Hit h = hit(paz, pel, true);
  if (previous.id && previous.id != h.id)
    event(windows[previous.id].data["owner"], "pointer_leave", {{"window_id", previous.id}});
  if (h.id)
    event(windows[h.id].data["owner"], "pointer_motion",
          {{"window_id", h.id},
           {"u", h.u},
           {"v", h.v},
           {"zone", h.zone},
           {"azimuth", paz},
           {"elevation", pel}});
  ++generation;
  return pointer_state(h.id ? &h : nullptr);
}
J WM::button(const std::string &name, bool pressed) {
  bool was = buttons.count(name);
  if (pressed)
    buttons.insert(name);
  else
    buttons.erase(name);
  Hit h = hit(paz, pel, true);
  if (!pressed && name == "left" && was) {
    ++release_serial;
    last_release = {{"azimuth", paz}, {"elevation", pel}};
  }
  if (pressed && name == "left" && !was) {
    ++press_serial;
    last_press = {{"azimuth", paz}, {"elevation", pel}, {"window_id", nullable(h.id)}};
  }
  if (pressed && name == "left" && h.id) {
    focus_window(h.id);
    if (h.zone == "title")
      drag = h.id;
  }
  if (!pressed && name == "left")
    drag = 0;
  if (h.id)
    event(windows[h.id].data["owner"], "pointer_button",
          {{"window_id", h.id},
           {"button", name},
           {"pressed", pressed},
           {"u", h.u},
           {"v", h.v},
           {"zone", h.zone},
           {"azimuth", paz},
           {"elevation", pel},
           {"cancel_serial", cancel_serial}});
  if (!pressed && name == "left")
    capture = 0;
  ++generation;
  return pointer_state(h.id ? &h : nullptr);
}
J WM::input_button(const std::string &owner, const std::string &name, bool pressed) {
  require(name == "left" || name == "middle" || name == "right",
          "button must be left, middle, or right");
  bool before = false, after = false;
  for (auto &v : input_sources)
    before |= v.second.count(name) != 0;
  auto &s = input_sources[owner];
  if (pressed)
    s.insert(name);
  else {
    s.erase(name);
    if (s.empty())
      input_sources.erase(owner);
  }
  for (auto &v : input_sources)
    after |= v.second.count(name) != 0;
  return before != after ? button(name, after) : pointer_state();
}
J WM::cancel() {
  auto h = hit(paz, pel, true);
  if (h.id)
    event(windows[h.id].data["owner"], "pointer_cancel",
          {{"window_id", h.id},
           {"u", h.u},
           {"v", h.v},
           {"zone", h.zone},
           {"azimuth", paz},
           {"elevation", pel}});
  buttons.clear();
  capture = drag = 0;
  ++cancel_serial;
  ++generation;
  return pointer_state();
}
J WM::state() const {
  J ws = J::array();
  for (auto id : order)
    ws.push_back(public_window(id));
  return {{"generation", generation},
          {"compositor", "cpp-native"},
          {"focused_window", nullable(focus)},
          {"pointer", pointer_state()},
          {"windows", ws}};
}
J WM::dispatch(const J &r) {
  std::string op = r.at("op"), owner = r.value("app", std::string("anonymous"));
  J a = r.value("args", J::object());
  int id = a.value("window_id", 0);
  if (op == "create_window") {
    require(windows.size() < 64, "window limit reached");
    int sw = a.value("surface_width", 320), sh = a.value("surface_height", 200);
    double wd = a.value("width_deg", 34.), hd = a.value("height_deg", 24.);
    require(sw >= 1 && sh >= 1 && int64_t(sw) * sh <= 1024 * 1024, "invalid surface size");
    require(wd >= 2 && wd <= 160 && hd >= 2 && hd <= 120, "invalid angular size");
    Window w;
    id = next_id++;
    w.data = {{"window_id", id},
              {"owner", owner},
              {"title", a.at("title").get<std::string>().substr(0, 96)},
              {"azimuth", azimuth(a.value("azimuth", 0.))},
              {"elevation", elevation(a.value("elevation", 0.))},
              {"roll", 0.},
              {"width_deg", wd},
              {"height_deg", hd},
              {"surface_width", sw},
              {"surface_height", sh},
              {"mapped", true},
              {"always_on_top", a.value("always_on_top", false)},
              {"decorated", a.value("decorated", false)},
              {"input_enabled", a.value("input_enabled", true)},
              {"workspace", workspace_validate(a.value("workspace", J::object()))},
              {"shape", shape_validate(a.value("shape", J::array()), sw, sh)}};
    // Allocate only if a mutable RGB surface is actually needed.
    w.dirty = {0, 0, sw, sh};
    windows[id] = std::move(w);
    if (windows[id].data["always_on_top"].get<bool>())
      order.push_back(id);
    else {
      auto it = std::find_if(order.begin(), order.end(),
                             [&](int k) { return windows[k].data["always_on_top"].get<bool>(); });
      order.insert(it, id);
    }
    if (a.value("focus_on_create", true) && a.value("input_enabled", true))
      set_focus(id);
    ++generation;
    return public_window(id);
  }
  if (op == "configure_window") {
    auto &w = window(id, &owner);
    auto &d = w.data;
    if (a.contains("title"))
      d["title"] = a["title"].get<std::string>().substr(0, 96);
    for (auto key : {"azimuth", "roll"})
      if (a.contains(key))
        d[key] = azimuth(a[key]);
    if (a.contains("elevation"))
      d["elevation"] = elevation(a["elevation"]);
    for (auto key : {"width_deg", "height_deg"})
      if (a.contains(key)) {
        double v = a[key];
        require(v >= 2 && v <= (std::string(key) == "width_deg" ? 160 : 120),
                "invalid angular size");
        d[key] = v;
      }
    for (auto key : {"mapped", "always_on_top", "decorated", "input_enabled"})
      if (a.contains(key))
        d[key] = a[key].get<bool>();
    if (a.contains("shape"))
      d["shape"] = shape_validate(a["shape"], d["surface_width"], d["surface_height"]);
    if (a.contains("workspace"))
      d["workspace"] = workspace_validate(a["workspace"]);
    if (!d["mapped"].get<bool>() || !d["input_enabled"].get<bool>()) {
      if (capture == id)
        capture = 0;
      if (drag == id)
        drag = 0;
      if (focus == id)
        focus_visible();
    }
    ++generation;
    return public_window(id);
  }
  if (op == "destroy_window") {
    destroy(owner, id);
    return true;
  }
  if (op == "focus_window")
    return focus_window(id, a.value("raise_window", true));
  if (op == "raise_window" || op == "lower_window") {
    window(id, &owner);
    if (op == "raise_window")
      raise(id);
    else {
      order.erase(std::find(order.begin(), order.end(), id));
      order.insert(order.begin(), id);
    }
    ++generation;
    return true;
  }
  if (op == "fill" || op == "update_surface") {
    auto &w = window(id, &owner);
    auto pending=w.dirty_regions;
    if(!w.dirty[2])pending.clear();
    else if(w.dirty_regions_revision!=w.revision)pending={{w.dirty[0],w.dirty[1],w.dirty[2],w.dirty[3]}};

    int sw = w.data["surface_width"], sh = w.data["surface_height"];
    if (op == "fill") {
      auto c = a.at("rgb");
      require(c.is_array() && c.size() == 3, "rgb must contain three bytes");
      for (auto &v : c)
        require(v.is_number_integer() && v.get<int>() >= 0 && v.get<int>() <= 255,
                "rgb must contain three bytes");
      w.surface.resize(size_t(sw)*sh*3);
      fill_rgb(w.surface.data(), w.surface.size(),
               {c[0].get<uint8_t>(),c[1].get<uint8_t>(),c[2].get<uint8_t>()});
      pending={{0,0,sw,sh}};
    } else {
      int x=a.at("x"), y=a.at("y"), ww=a.at("width"), hh=a.at("height");
      require(x >= 0 && y >= 0 && ww >= 1 && hh >= 1 && int64_t(x) + ww <= sw &&
                  int64_t(y) + hh <= sh,
              "surface update is out of bounds");
      auto raw = unbase64(a.at("rgb24"));
      require(raw.size() == size_t(ww) * hh * 3, "rgb24 byte count does not match rectangle");
      w.ensure_surface();
      auto changed=patch_rgb_tiles(w.surface,sw,{x,y,ww,hh},raw);
      if(changed.empty())return true;
      pending.insert(pending.end(),changed.begin(),changed.end());
    }
    w.prepared.reset();
    w.dirty_regions=tiled_regions(pending,sw,sh);
    Rect dirty{};
    for(auto r:w.dirty_regions)dirty=bounds(dirty,r);
    w.dirty={dirty.x,dirty.y,dirty.w,dirty.h};
    ++w.revision;w.dirty_regions_revision=w.revision;
    w.solid_revision=op=="fill"?w.revision:0;
    if(op=="fill"){auto c=a.at("rgb");w.solid_index=(c[0].get<unsigned>()&224)|((c[1].get<unsigned>()&224)>>3)|(c[2].get<unsigned>()>>6);}
    ++generation;
    return true;
  }
  if (op == "pointer_warp" || op == "input_warp")
    return warp(a.at("azimuth"), a.at("elevation"));
  if (op == "pointer_move" || op == "input_move")
    return warp(paz + a.at("delta_azimuth").get<double>(),
                pel + a.at("delta_elevation").get<double>());
  if (op == "pointer_button")
    return button(a.value("button", std::string("left")), a.at("pressed"));
  if (op == "input_button")
    return input_button(owner, a.value("button", std::string("left")), a.at("pressed"));
  if (op == "input_click") {
    std::string b = a.value("button", std::string("left"));
    require(!input_sources[owner].count(b), "input source already holds this button");
    input_button(owner, b, true);
    return input_button(owner, b, false);
  }
  if (op == "input_status") {
    auto p = pointer_state();
    p["source_buttons"] = input_sources.count(owner) ? J(input_sources.at(owner)) : J::array();
    p["active_button_source_count"] = input_sources.size();
    return p;
  }
  if (op == "capture_pointer") {
    auto &w = window(id, &owner);
    if (!w.data["mapped"].get<bool>() || !w.data["input_enabled"].get<bool>() ||
        !buttons.count("left"))
      return false;
    require(!capture || windows[capture].data["owner"] == owner,
            "pointer is captured by another application");
    capture = id;
    drag = 0;
    return true;
  }
  if (op == "release_pointer") {
    if (capture)
      window(capture, &owner);
    capture = 0;
    return true;
  }
  if (op == "cancel_pointer") {
    input_sources.clear();
    return cancel();
  }
  if (op == "input_scroll") {
    ++scroll_serial;
    scroll_delta = finite(a.at("delta"));
    ++generation;
    return pointer_state();
  }
  if (op == "request_window_action") {
    auto &w = window(id);
    std::string action = a.at("action");
    require(w.data["workspace"].value("role", std::string()) == "app" &&
                (action == "minimize" || action == "restore" || action == "close"),
            "window does not accept workspace actions");
    J p = {{"window_id", id}, {"action", action}, {"requester", owner}};
    if ((a.contains("azimuth") && !a["azimuth"].is_null()) ||
        (a.contains("elevation") && !a["elevation"].is_null())) {
      p["azimuth"] = azimuth(a.at("azimuth"));
      p["elevation"] = elevation(a.at("elevation"));
    }
    event(w.data["owner"], "window_action", p);
    return true;
  }
  if (op == "input_key") {
    std::string key = a.at("key");
    bool pressed = a.value("pressed", true);
    require(key == "tab" || key == "enter" || key == "space" || key == "escape" || key == "next",
            "unsupported workspace key");
    if (key == "escape" && pressed) {
      input_sources.clear();
      cancel();
    }
    if (key == "next" && pressed) {
      std::vector<int> ids;
      for (int k : order)
        if (windows[k].data["mapped"].get<bool>() && windows[k].data["input_enabled"].get<bool>())
          ids.push_back(k);
      if (!ids.empty()) {
        auto it = std::find(ids.begin(), ids.end(), focus);
        size_t n = it == ids.end() ? 0 : (it - ids.begin() + 1) % ids.size();
        focus_window(ids[n], false);
      }
      return true;
    }
    if (focus && windows[focus].data["mapped"].get<bool>() &&
        windows[focus].data["input_enabled"].get<bool>())
      event(windows[focus].data["owner"], "key",
            {{"window_id", focus}, {"key", key}, {"pressed", pressed}});
    return true;
  }
  if (op == "poll_events") {
    auto &q = events[owner];
    J out = J::array();
    int n = std::max(0, a.value("limit", 64));
    while (n-- && !q.empty()) {
      out.push_back(q.front());
      q.pop_front();
    }
    return out;
  }
  if (op == "get_state")
    return state();
  if (op == "view_move" || op == "view_set" || op == "view_reset" || op == "view_state" || op == "view_click") {
    if (op == "view_move" || op == "view_set" || op == "view_reset") {
      double dy = finite(a.value("yaw", 0.)), dp = finite(a.value("pitch", 0.)), dr = finite(a.value("roll", 0.));
      bool absolute = op == "view_set";
      double yaw = op == "view_reset" ? 0 : azimuth((absolute ? 0 : view_yaw) + dy),
             pitch = op == "view_reset" ? 0 : std::clamp((absolute ? 0 : view_pitch) + dp, -85., 85.);
      double roll = op == "view_reset" ? 0 : azimuth((absolute ? 0 : view_roll) + dr);
      if (pose)
        pose(yaw, pitch, roll);
      view_yaw = yaw;
      view_pitch = pitch;
      view_roll = roll;
      warp(yaw, pitch);
    }
    J result = {{"yaw", view_yaw}, {"pitch", view_pitch}};
    if (view_roll != 0 || a.contains("roll")) result["roll"] = view_roll;
    if (op == "view_click") {
      warp(view_yaw, view_pitch);
      result["pointer"] =
          dispatch({{"op", "input_click"}, {"app", owner}, {"args", {{"button", "left"}}}});
    }
    return result;
  }
  throw std::runtime_error("unknown operation");
}
} // namespace boayo
