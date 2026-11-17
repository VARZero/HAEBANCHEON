#include "ui.hpp"
#include "init_trace.hpp"
#include "font.hpp"
#include "rgb_fill.hpp"
#include "assets.hpp"
#include <cctype>
#include <cstdio>
namespace boayo {
void Surface::clear(Color c) {
  prepared.reset();pixels.resize(size_t(width)*height*3);
  init_trace::mark("surface.clear.begin", pixels.size());
  fill_rgb(pixels.data(), pixels.size(), c);
  init_trace::mark("surface.clear.end", pixels.size());
}
void Surface::blend(int x, int y, unsigned n, Color c) {
  mutate();
  if (x < 0 || y < 0 || x >= width || y >= height)
    return;
  size_t p = (size_t(y) * width + x) * 3;
  for (unsigned k = 0; k < 3; k++)
    pixels[p + k] = (pixels[p + k] * (16 - n) + c[k] * n + 8) / 16;
}
void Surface::rect(double x, double y, double w, double h, Color c, bool fill, int stroke) {
  mutate();
  int x0 = std::max(0, int(x)), y0 = std::max(0, int(y)), x1 = std::min(width, int(x + w)),
      y1 = std::min(height, int(y + h)), s = std::max(1, stroke);
  for (int yy = y0; yy < y1; yy++)
    for (int xx = x0; xx < x1; xx++)
      if (fill || yy < y0 + s || yy >= y1 - s || xx < x0 + s || xx >= x1 - s)
        for (int k = 0; k < 3; k++)
          pixels[(size_t(yy) * width + xx) * 3 + k] = c[k];
}
void Surface::rounded(double xx, double yy, double ww, double hh, double rr, Color c) {
  init_trace::mark("rounded.begin");
  int x = xx, y = yy, w = ww, h = hh, r = std::max(0, std::min({int(rr), w / 2, h / 2}));
  rect(x + r, y, w - 2 * r, h, c);
  rect(x, y + r, w, h - 2 * r, c);
  if (!r) { init_trace::mark("rounded.end", 0); return; }
  for (int iy = 0; iy < r; iy++) {
    if (init_trace::active && (iy == 0 || iy == r/2 || iy == r-1)) init_trace::mark("rounded.row", iy);
    for (int ix = 0; ix < r; ix++) {
      unsigned n = 0;
      for (double oy : {.125, .375, .625, .875})
        for (double ox : {.125, .375, .625, .875}) {
          double dx = ix + ox - r, dy = iy + oy - r;
          n += dx * dx + dy * dy <= r * r;
        }
      blend(x + ix, y + iy, n, c);
      blend(x + w - 1 - ix, y + iy, n, c);
      blend(x + ix, y + h - 1 - iy, n, c);
      blend(x + w - 1 - ix, y + h - 1 - iy, n, c);
    }
  }
  init_trace::mark("rounded.end", r);
}
void Surface::circle(double cx, double cy, double r, Color c) {
  init_trace::mark("circle.begin");
  for (int y = std::max(0, int(std::floor(cy - r))); y < std::min(height, int(std::ceil(cy + r)));
       y++) {
    if (init_trace::active && (y % 8 == 0)) init_trace::mark("circle.row", y);
    for (int x = std::max(0, int(std::floor(cx - r))); x < std::min(width, int(std::ceil(cx + r)));
         x++) {
      unsigned n = 0;
      for (double oy : {.125, .375, .625, .875})
        for (double ox : {.125, .375, .625, .875})
          n += (x + ox - cx) * (x + ox - cx) + (y + oy - cy) * (y + oy - cy) <= r * r;
      blend(x, y, n, c);
    }
  }
  init_trace::mark("circle.end");
}
void Surface::polygon(const std::vector<std::array<float, 2>> &p, Color c) {
  if (p.empty())
    return;
  float minx = p[0][0], maxx = minx, miny = p[0][1], maxy = miny;
  for (auto v : p) {
    minx = std::min(minx, v[0]);
    maxx = std::max(maxx, v[0]);
    miny = std::min(miny, v[1]);
    maxy = std::max(maxy, v[1]);
  }
  for (int y = std::max(0, int(std::floor(miny))); y < std::min(height, int(std::ceil(maxy)) + 1);
       y++)
    for (int x = std::max(0, int(std::floor(minx))); x < std::min(width, int(std::ceil(maxx)) + 1);
         x++) {
      unsigned n = 0;
      for (double oy : {.125, .375, .625, .875})
        for (double ox : {.125, .375, .625, .875}) {
          bool in = false;
          auto prev = p.back();
          for (auto cur : p) {
            double d = float(cur[1] - prev[1]);
            if (!d)
              d = 1e-6;
            if ((prev[1] > y + oy) != (cur[1] > y + oy) &&
                x + ox < float(cur[0] - prev[0]) * (y + oy - prev[1]) / d + prev[0])
              in = !in;
            prev = cur;
          }
          n += in;
        }
      blend(x, y, n, c);
    }
}
void Surface::text(const std::string &value, int x, int y, Color c, int scale, bool bold) {
  scale = std::max(1, scale);
  for (unsigned char ch : value) {
    unsigned code = std::toupper(ch);
    if (code >= 128)
      code = ' ';
    for (int row = 0; row < 7; row++)
      for (int col = 0; col < 5; col++)
        if (font[code][row] & (1 << (4 - col)))
          rect(x + col * scale, y + row * scale, scale + int(bold), scale, c);
    x += (bold ? 7 : 6) * scale;
  }
}
void Surface::blit(const Surface &s, int x, int y) {
  s.ensure_pixels();
  mutate();
  for (int yy = 0; yy < s.height; yy++)
    for (int xx = 0; xx < s.width; xx++)
      if (x + xx >= 0 && x + xx < width && y + yy >= 0 && y + yy < height)
        std::copy_n(s.pixels.data() + (size_t(yy) * s.width + xx) * 3, 3,
                    pixels.data() + (size_t(y + yy) * width + x + xx) * 3);
}
Surface caption_image(const std::string &name, bool selected, int height) {
  if(auto image=asset_surface(J::array({"caption",name,selected,height})))return std::move(*image);
  Surface c(40, height);
  c.rounded(0, 0, 40, height, 8, selected ? focus_color : edge);
  Color fill = name == "close" ? danger : (selected ? Color{96, 128, 160} : Color{64, 64, 64});
  c.rounded(1, 1, 38, height - 2, 7, fill);
  if (name == "move") {
    for (int x : {15, 24})
      for (int y : {11, 20, 29})
        c.circle(x, y + (height - 40) / 2, 2, text_color);
  } else if (name == "close") {
    for (int s = 0; s < 18; s++) {
      c.rect(10 + s, 10 + s, 3, 3, text_color);
      c.rect(27 - s, 10 + s, 3, 3, text_color);
    }
  } else {
    for (int s = 0; s < 18; s++)
      c.rect(10 + s, 27 - s, 3, 3, text_color);
    c.rect(19, 10, 11, 3, text_color);
    c.rect(27, 10, 3, 11, text_color);
    c.rect(10, 19, 3, 11, text_color);
    c.rect(10, 27, 11, 3, text_color);
  }
  return c;
}
Surface launcher_image(const J &apps, int selected, int hovered, int offset, bool visible,
                       bool active, int width, int height) {
  if(auto image=asset_surface(J::array({"launcher",apps,selected,hovered,offset,visible,active,width,height})))return std::move(*image);
  init_trace::mark("launcher.allocate.begin", static_cast<long long>(width)*height*3);
  Surface c(width, height);
  init_trace::mark("launcher.allocate.end", c.pixels.size());
  if (!visible || active)
    return c;
  double rx = width * .08, ry = height * .10, rw = width * .84, rh = height * .70;
  c.rounded(rx, ry, rw, rh, 18, edge);
  c.rounded(rx + 2, ry + 2, rw - 4, rh - 4, 16, chrome);
  int x = rx, y = ry, w = rw, h = rh, split = x + int(w * .34);
  c.rect(split - 3, y + 22, 1, h - 44, edge);
  c.circle(x + 52, y + 46, 18, edge);
  c.circle(x + 52, y + 46, 15, chrome);
  c.circle(x + 52, y + 46, 5, focus_color);
  c.rect(x + 30, y + 45, 8, 3, focus_color);
  c.rect(x + 66, y + 45, 8, 3, focus_color);
  init_trace::mark("launcher.text.begin");
  c.text("BOAYO", x + 24, y + 80, text_color, 3, true);
  c.text("WORKSPACE", x + 24, y + 111, subtle, 2);
  c.text("LOOK / HOLD", x + 24, y + 147, subtle, 1);
  c.text("DOWN TO STORE", x + 24, y + 166, subtle, 1);
  c.text("BTN1: APP LIST", x + 24, y + h - 37, subtle, 1);
  init_trace::mark("launcher.text.end");
  int lx = split + 20, ly = y + 22, n = std::max(1, std::min(5, (h - 38) / 54));
  for (int row = 0; row < n; row++) {
    int index = offset + row;
    if (index >= int(apps.size()))
      break;
    auto a = apps[index];
    init_trace::mark("launcher.app.begin", index);
    int bx = lx, by = ly + row * 54;
    bool sel = index == selected || index == hovered;
    if (sel)
      c.rounded(bx, by, x + w - bx - 18, 49, 9, raised);
    Color color = accent;
    std::string s = a.value("color", std::string());
    if (s.size() == 7 && s[0] == '#')
      for (int k = 0; k < 3; k++)
        color[k] = std::stoul(s.substr(1 + k * 2, 2), nullptr, 16);
    c.rounded(bx + 8, by + 5, 38, 38, 11, color);
    c.text(std::to_string(index + 1), bx + 22, by + 15, text_color, 2, true);
    c.text(a.value("name", a.value("id", std::string("APP"))).substr(0, 9), bx + 56, by + 10,
           text_color, 3, true);
    c.text("OPEN AT GAZE", bx + 56, by + 35, sel ? focus_color : subtle, 1);
    if (sel)
      c.rect(bx, by + 8, 3, 33, focus_color);
  }
  init_trace::mark("launcher.apps.end");
  if (int(apps.size()) > n)
    c.text("WHEEL", lx + 8, y + h - 27, subtle, 2, true);
  init_trace::mark("launcher.image.end", c.pixels.size());
  return c;
}
Surface app_frame(const std::string &title, int w, int h, bool focused, const std::string &hovered,
                  bool external) {
  if(auto image=asset_surface(J::array({"frame",title,w,h,focused,hovered,external})))return std::move(*image);
  require(w >= 240 && h >= 170, "app surface too small");
  Surface c(w, h);
  int sx = 48, sy = 4, sw = w - 96, sh = h - 12, cx = 50, cy = 48, cw = w - 100, ch = h - 82;
  c.rounded(sx, sy, sw, sh, 14, edge);
  c.rounded(sx + 1, sy + 1, sw - 2, sh - 2, 13, chrome);
  c.rect(cx, 6, cw, 40, {64, 96, 128});
  c.rect(cx, cy, cw, ch, white);
  c.rect(cx, 46, cw, 1, edge);
  int bottom = cy + ch;
  c.rect(cx, bottom, cw, 1, edge);
  c.rect(cx, bottom + 1, cw, 24, {32, 64, 96});
  c.circle(sx + 14, 25, 3, focused ? focus_color : subtle);
  c.text(title.substr(0, std::max(1, (sw - 43) / 14)), sx + 27, 18, text_color, 2, true);
  std::string label = focused ? "ACTIVE" : "SPATIAL";
  if (hovered == "move")
    label = "MOVE / DOWN TO STORE";
  else if (hovered == "resize_right")
    label = "RESIZE";
  else if (hovered == "close")
    label = "CLOSE";
  c.text(label, cx + 10, bottom + 9, subtle, 1);
  Surface mask(w, h, {0, 0, 0});
  mask.rounded(sx, sy, sw, sh, 14, {255, 255, 255});
  for (size_t p = 0; p < c.pixels.size(); p += 3) {
    float alpha = mask.pixels[p] / 255.f;
    for (int k = 0; k < 3; k++)
      c.pixels[p + k] = uint8_t(
          std::nearbyint(float(c.pixels[p + k] * alpha) + float(black[k] * float(1 - alpha))));
  }
  if (focused)
    c.rect(sx + 14, sy, sw - 28, 1, focus_color);
  if (focused && !external) {
    c.blit(caption_image("move", hovered == "move"), 4, h / 2 - 20);
    c.blit(caption_image("resize_right", hovered == "resize_right"), w - 44, h - 46);
    c.blit(caption_image("close", hovered == "close"), w - 44, 6);
  }
  return c;
}
Surface storage_image(const std::string &title, bool selected, bool dim, bool closing) {
  if(auto image=asset_surface(J::array({"storage",title,selected,dim,closing})))return std::move(*image);
  Surface c(320, 96);
  Color b = closing ? danger : (selected ? focus_color : edge), v = selected ? raised : chrome;
  if (dim) {
    for (auto &x : b)
      x /= 2;
    for (auto &x : v)
      x /= 2;
  }
  c.rounded(2, 2, 316, 92, 18, b);
  c.rounded(5, 5, 310, 86, 15, v);
  for (int x : {16, 24})
    for (int y : {31, 43, 55})
      c.circle(x, y, 2, subtle);
  c.text(title.substr(0, 12), 42, 20, text_color, 3, true);
  c.text(selected ? "UP: OPEN  DOWN: CLOSE" : "STORED / LOOK TO OPEN", 42, 61,
         selected ? focus_color : subtle, 1);
  return c;
}
Surface feedback_image(const std::string &name, const std::string &label, Color color) {
  if(auto image=asset_surface(J::array({"feedback",name,label,color})))return std::move(*image);
  Surface c(320, name == "preview" ? 240 : 96);
  c.rounded(0, 0, c.width, c.height, 12, color);
  c.rounded(4, 4, c.width - 8, c.height - 8, 9, chrome);
  if (name == "preview") {
    c.rect(4, 58, c.width - 8, 2, color);
    c.text("RELEASE TO PLACE", 22, 108, text_color, 2);
    c.text("ESC / BTN3: CANCEL", 22, 180, subtle, 1);
  }
  c.text(label.substr(0, 13), 13, name == "preview" ? 16 : 30, color, 3, true);
  return c;
}
Surface app_image(const std::string &app, const J &s) {
  int w = s.value("width", app == "dashboard" ? 640 : 400),
      h = s.value("height", app == "dashboard" ? 440 : 300);
  bool focused = s.value("focused", true);
  std::string title = app == "dashboard"   ? "Dashboard"
                      : app == "telemetry" ? "Telemetry"
                                           : "SDK Pulse";
  auto c = app_frame(title, w, h, focused, s.value("hovered", std::string()), true);
  c.mutate(); // Materialize only when dynamic contents actually change.
  auto chrome_pixels = c.pixels;
  int x = 50, y = 48, cw = w - 100, ch = h - 82;
  char buf[128];
  if (app == "dashboard") {
    int count = s.value("apps", 1), stored = s.value("stored", 0);
    c.rect(x, y, cw, ch, chrome);
    c.text("YOUR WORKSPACE", x + 22, y + 25, text_color, 3, true);
    for (int col = 0; col < 2; col++) {
      int tx = x + 22 + col * (cw / 2);
      c.rounded(tx, y + 80, cw / 2 - 36, 98, 12, raised);
      c.text(col ? "STORED" : "OPEN", tx + 16, y + 92, subtle, 2);
      c.text(std::to_string(col ? stored : count - stored), tx + 16, y + 123, focus_color, 4, true);
    }
    std::snprintf(buf, sizeof(buf), "YAW %+.0f  PITCH %+.0f", s.value("yaw", 0.),
                  s.value("pitch", 0.));
    c.text(buf, x + 22, y + 210, text_color, 2);
    c.text("BTN1 APPS / BTN2 HOLD TO MOVE", x + 22, y + 251, subtle, 2);
    c.text("LOOK DOWN TO FIND STORED WINDOWS", x + 22, y + 291, subtle, 1);
  } else if (app == "telemetry") {
    int t = s.value("step", 0);
    c.text("TELEMETRY", x + 22, y + 18, ink, 3, true);
    int pulse = (std::sin(t * .12) + 1) * .5 * (cw - 48);
    c.rounded(x + 22, y + 76, cw - 44, 14, 7, {211, 220, 230});
    if (pulse > 10)
      c.rounded(x + 22, y + 76, pulse, 14, 7, {51, 181, 122});
    c.rounded(x + 22, y + 120, cw - 44, 10, 5, {211, 220, 230});
    c.rounded(x + 22, y + 120, 14 + (t * 7) % (cw - 58), 10, 5, {52, 124, 255});
  } else {
    int xx = x + 24, ww = cw - 48, step = s.value("step", 0), clicks = s.value("clicks", 0);
    c.text("SDK PULSE", xx, y + 22, ink, 3, true);
    c.text(focused ? "FOCUSED" : "UNFOCUSED", xx, y + 68, {64, 64, 64}, 2);
    c.rounded(xx, y + 103, ww, 24, 10, {224, 232, 240});
    c.rounded(xx, y + 103, std::max(12, int(ww * (step + 1) / 10)), 24, 10, {52, 124, 255});
    std::snprintf(buf, sizeof(buf), "CLICKS %02d", clicks);
    c.text(buf, xx, y + 151, ink, 3, true);
    std::snprintf(buf, sizeof(buf), "%.1f x %.1f DEG", s.value("width_deg", 38.),
                  s.value("height_deg", 28.));
    c.text(buf, xx, y + 185, {64, 64, 64}, 1);
  }
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      if (yy < y || yy >= y + ch || xx < x || xx >= x + cw)
        std::copy_n(chrome_pixels.data() + (size_t(yy) * w + xx) * 3, 3,
                    c.pixels.data() + (size_t(yy) * w + xx) * 3);
  return c;
}
J ui_test(const J &r) {
  std::string type = r.at("kind");
  Surface s;
  if (type == "caption")
    s = caption_image(r.value("name", std::string("move")), r.value("selected", false),
                      r.value("height", 40));
  else if (type == "launcher")
    s = launcher_image(r.at("apps"), r.value("selected", -1), r.value("hovered", -1),
                       r.value("offset", 0), r.value("visible", true), r.value("active", false));
  else if (type == "frame")
    s = app_frame(r.value("title", std::string("Example")), r.value("width", 400),
                  r.value("height", 300), r.value("focused", false),
                  r.value("hovered", std::string()), r.value("external", true));
  else if (type == "storage")
    s = storage_image(r.at("title"), r.value("selected", false), r.value("dim", false),
                      r.value("closing", false));
  else if (type == "feedback")
    s = feedback_image(r.at("name"), r.at("label"), r.value("color", J(focus_color)).get<Color>());
  else if (type == "app")
    s = app_image(r.at("app"), r.value("state", J::object()));
  else
    throw std::runtime_error("unknown UI image");
  s.ensure_pixels();
  write_bytes(r.at("path"), s.pixels.data(), s.pixels.size());
  return {{"width", s.width}, {"height", s.height}, {"bytes", s.pixels.size()}};
}
} // namespace boayo
