#include "backend.hpp"
#include "apps.hpp"
#include "ui.hpp"
#include "golden.hpp"
#include <iostream>
namespace boayo {
static uint32_t fnv(const void *p, size_t n) {
  auto *b = static_cast<const uint8_t *>(p);
  uint32_t v = 2166136261u;
  while (n--)
    v = (v ^ *b++) * 16777619u;
  return v;
}
int selftest(bool dual) {
  Geometry geometry;
  auto a = geometry.camera(), b = geometry.camera(37, -29, 13);
  require(fnv(a.data(), sizeof(a)) == golden_camera[0] &&
              fnv(b.data(), sizeof(b)) == golden_camera[1],
          "Python golden camera mismatch");
  unsigned ids[] = {0, 211, 1234, 4219};
  for (unsigned i = 0; i < 4; i++) {
    auto d = geometry.descriptor(ids[i], 640, 360, 37, -29, 13, 42, 30).first;
    require(fnv(d.data(), sizeof(d)) == golden_descriptor[i],
            "Python golden projection descriptor mismatch");
  }
  unsigned i = 0;
  for (auto name : {"move", "resize_right", "close"}) {
    auto c = caption_image(name, true, std::string(name) == "move" ? 100 : 40);
    require(fnv(c.pixels.data(), c.pixels.size()) == golden_caption[i++],
            "Python golden caption mismatch");
  }
  WM wm;
  auto call = [&](std::string op, J args = J::object()) {
    return wm.dispatch({{"op", op}, {"args", args}, {"app", "selftest"}});
  };
  int id = call(
      "create_window",
      {{"title", "Native selftest"}, {"surface_width", 32}, {"surface_height", 24}})["window_id"];
  call("fill", {{"window_id", id}, {"rgb", {255, 0, 0}}});
  call("view_move", {{"yaw", 2}, {"pitch", 2}});
  call("view_reset");
  call("view_click");
  require(wm.release_serial == 1 && wm.focus == id, "WM view/click state");
  call("capture_pointer", {{"window_id", id}});
  require(wm.capture == 0, "capture without pressed button");
  {
    WM desktop_wm;
    Desktop desktop(desktop_wm);
    desktop.update(0);
    const auto owners = desktop_wm.events.size();
    const auto windows = desktop_wm.windows.size();
    for (int iteration = 0; iteration < 8; ++iteration) {
      int app = desktop.launch("dashboard");
      desktop.running.at(app)->close();
      desktop.update(iteration + 1);
      require(desktop.running.empty() && desktop_wm.events.size() == owners &&
                  desktop_wm.windows.size() == windows,
              "closed app leaked windows or owner event queues");
    }
  }
  if (dual) {
    MockDevice device;
    Projectors projectors(device);
    Output output(device, geometry);
    Compositor compositor(geometry, projectors);
    auto draw = [&]() {
      auto c = compositor.update(wm);
      if (c.patch)
        output.patch(c.data());
      else
        output.full(c.data());
      if (!c.patch || !c.data().empty())
        compositor.ack();
      Words expected = compositor.scene;
      for (unsigned k = 0; k < 256; k++) {
        auto v = expected[k];
        expected[k] = (v & 0xff0000) | ((v & 255) << 8) | ((v >> 8) & 255);
      }
      require(output.shadow == expected, "output shadow differs from composition");
      for (unsigned bank = 3; bank <= 4; bank++)
        require(std::equal(expected.begin(), expected.end(), device.regions[bank].begin()),
                "scanout bank differs from composition");
      return c;
    };
    draw();
    require(compositor.metrics["jobs_per_core"][0].get<unsigned>() > 0 &&
                compositor.metrics["jobs_per_core"][1].get<unsigned>() > 0,
            "both projectors must run");
    auto idle = draw();
    require(idle.patch && idle.words.empty(), "idle frame generated writes");
    call("fill", {{"window_id", id}, {"rgb", {0, 255, 0}}});
    auto changed = draw();
    require(changed.patch && !changed.words.empty(), "content change produced no patch");
    // A single tile patch copies only one directory entry and 256 words, not a full scene.
    Words packet(288, 0);
    packet[0] = 0x42505432;
    packet[1] = 256;
    packet[2] = 1;
    packet[3] = packet.size();
    packet[16] = 0;
    packet[17] = 0;
    packet[18] = 0;
    std::fill(packet.begin() + 32, packet.end(), 0x12345678);
    auto copied = output.shadow_copied_words;
    output.patch(packet);
    require(output.shadow_copied_words - copied == 257, "patch copied whole scene");
    require(device.regions[3] == device.regions[4], "patch bank mirror mismatch");
    for (int fail : {0, 2, 3}) {
      MockDevice broken;
      Output guarded(broken, geometry);
      guarded.full(compositor.scene);
      auto before = guarded.shadow;
      broken.fail_after = fail;
      bool caught = false;
      try {
        guarded.patch(packet);
      } catch (const std::runtime_error &) {
        caught = true;
      }
      require(caught && guarded.failed && guarded.shadow == before,
              "failed patch published shadow or did not poison output");
    }
    {
      MockDevice reused;
      std::fill(reused.regions[3].begin(), reused.regions[3].end(), 0xdeadbeef);
      std::fill(reused.regions[4].begin(), reused.regions[4].end(), 0xa5a5a5a5);
      Output sparse(reused, geometry);
      Words empty(scene_words, 0);
      std::fill(empty.begin() + 256, empty.begin() + header, 0xffffffff);
      sparse.full(empty, true);
      require(sparse.uploaded_words == header && sparse.dma_copied_words == header,
              "empty initialization must upload metadata once and DMA-mirror metadata only");
      for (unsigned bank : {3u, 4u})
        require(std::equal(sparse.shadow.begin(), sparse.shadow.begin() + header,
                           reused.regions[bank].begin()),
                "initial directory not overwritten");
      sparse.patch(packet);
      for (unsigned bank : {3u, 4u}) {
        require(reused.regions[bank][256] == 0, "tile not activated");
        require(
            std::equal(packet.begin() + 32, packet.end(), reused.regions[bank].begin() + header),
            "stale RAM exposed by activation");
      }
    }
    auto saved = output.camera;
    device.fail_after = 0;
    try {
      output.set_pose(10, 0);
      throw std::logic_error("failure injection not detected");
    } catch (const std::runtime_error &) {
      require(output.failed && output.camera == saved, "failed pose was committed");
    }
    std::cout << "BOAYO_DUAL_MODEL_PASS {\"scope\":\"integer model, not "
                 "FPGA\",\"both_cores\":true,\"bank_ownership\":true}"
              << std::endl;
  }
  std::cout << "BOAYO_NATIVE_SELFTEST_PASS "
               "{\"python_golden_geometry\":true,\"python_golden_pixels\":true,\"wm_input\":true,"
               "\"hardware_tested\":false}"
            << std::endl;
  return 0;
}
} // namespace boayo
