#pragma once
#include "common.hpp"
#include <memory>
#include "texture.hpp"
namespace boayo {
using Color = std::array<uint8_t, 3>;
inline constexpr Color black = {3, 5, 8}, white = {250, 250, 249}, ink = {22, 29, 40},
                       muted = {105, 116, 132}, panel = {239, 242, 246}, accent = {44, 112, 246};
inline constexpr Color chrome = {32, 32, 64}, raised = {64, 64, 96}, edge = {96, 128, 160},
                       focus_color = {128, 224, 192}, text_color = {250, 250, 249},
                       subtle = {160, 192, 192}, danger = {224, 96, 64};
struct Surface {
  int width, height;
  mutable Bytes pixels;
  void ensure_pixels() const {if(prepared)materialize_rgb(pixels,*prepared);}
  void mutate(){ensure_pixels();prepared.reset();}
  std::shared_ptr<const Texture> prepared;
  Surface(int w, int h, Bytes data, std::shared_ptr<const Texture> texture)
      : width(w), height(h), pixels(std::move(data)), prepared(std::move(texture)) {}
  Surface(int w = 640, int h = 360, Color color = black)
      : width(w), height(h), pixels(size_t(w) * h * 3) {
    clear(color);
  }
  void clear(Color c);
  void blend(int x, int y, unsigned coverage, Color c);
  void rect(double x, double y, double w, double h, Color c, bool fill = true, int stroke = 1);
  void rounded(double x, double y, double w, double h, double radius, Color c);
  void circle(double x, double y, double radius, Color c);
  void polygon(const std::vector<std::array<float, 2>> &points, Color c);
  void text(const std::string &value, int x, int y, Color c = ink, int scale = 2,
            bool bold = false);
  void blit(const Surface &source, int x, int y);
};
Surface caption_image(const std::string &name, bool selected = false, int height = 40);
Surface launcher_image(const J &apps, int selected = -1, int hovered = -1, int offset = 0,
                       bool visible = true, bool active = false, int width = 640, int height = 360);
Surface app_frame(const std::string &title, int w, int h, bool focused,
                  const std::string &hovered = "", bool external = true);
Surface storage_image(const std::string &title, bool selected = false, bool dim = false,
                      bool closing = false);
Surface feedback_image(const std::string &name, const std::string &label, Color color);
Surface app_image(const std::string &app, const J &state);
J ui_test(const J &request);
} // namespace boayo
