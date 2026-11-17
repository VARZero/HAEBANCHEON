#include "wm.hpp"
#include "geometry.hpp"
#include "ui.hpp"
#include "backend.hpp"
#include "apps.hpp"
#include "runtime.hpp"
#include "rgb_fill.hpp"
#include "assets.hpp"
#include <iostream>
using namespace boayo;
int main(int argc, char **argv) {
  try {
    if (argc < 2 || std::string(argv[1]) != "--rpc")
      return runtime_main(argc, argv);
    WM wm;
    MockDevice rgb_mock;
    RgbFillScope rgb_scope({&rgb_mock, [](void *d,uint8_t *p,size_t n,
                                         const std::array<uint8_t,3> &c) {
      const char *enabled=std::getenv("BOAYO_TEST_RGB_DMA");
      return enabled && std::string(enabled)=="1" && static_cast<MockDevice *>(d)->rgb_fill(p,n,c);
    }});
    Geometry geometry;
    std::unique_ptr<MockDevice> mock;
    std::unique_ptr<Projectors> projectors;
    std::unique_ptr<Compositor> compositor;
    std::unique_ptr<Output> output;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<App> app;
    if (argc > 1 && std::string(argv[1]) == "--rpc") {
      std::string line;
      while (std::getline(std::cin, line)) {
        J id = nullptr;
        try {
          J r = J::parse(line);
          id = r.value("id", J(nullptr));
          std::string op = r.at("op");
          J result;
          if (op == "test_asset_stats") {
            result=asset_status();
          } else if (op == "test_rgb_dma_stats") {
            result={{"calls",rgb_mock.dma_fills}};
          } else if (op == "test_camera") {
            auto a = r.at("args");
            result = geometry.camera(a.value("yaw", 0.), a.value("pitch", 0.), a.value("roll", 0.));
          } else if (op == "test_descriptors") {
            auto a = r.at("args");
            Words words;
            Bytes visible;
            for (unsigned t = 0; t < tiles; t++) {
              auto [d, v] =
                  geometry.descriptor(t, a.value("width", 640u), a.value("height", 360u),
                                      a.value("yaw", 0.), a.value("pitch", 0.), a.value("roll", 0.),
                                      a.value("fov_h", 42.), a.value("fov_v", 30.));
              words.insert(words.end(), d.begin(), d.end());
              visible.push_back(v);
            }
            std::string p = a.at("path");
            write_bytes(p + ".bin", words.data(), words.size() * 4);
            write_bytes(p + ".visible", visible.data(), visible.size());
            result = true;
          } else if (op == "test_project") {
            auto a = r.at("args");
            Descriptor d = a.at("descriptor").get<Descriptor>();
            Words texture = a.at("texture").get<Words>(), scene = a.at("scene").get<Words>();
            auto cells = project_reference(texture, scene, d);
            result = {{"scene", scene}, {"cells", cells}};
          } else if (op == "test_surface") {
            auto &w = wm.window(r.at("args").at("window_id"));
            result = base64(w.surface);
          } else if (op == "test_disconnect") {
            wm.disconnect(r.at("args").at("owner"));
            result = true;
          } else if (op == "test_ui")
            result = ui_test(r.at("args"));
          else if (op == "test_app") {
            auto a = r.at("args");
            std::string command = a.at("op");
            double now = a.value("now", 0.);
            if (command == "create")
              app = std::make_unique<App>(wm, "test", a.value("kind", std::string("sdk-pulse")),
                                          a.value("azimuth", 0.), a.value("elevation", 0.));
            else {
              require(bool(app), "app not created");
              if (command == "tick")
                app->tick(now);
              else if (command == "event")
                app->handle(a.at("event"), now);
              else if (command == "minimize")
                app->minimize(now);
              else if (command == "restore")
                app->restore(a);
              else if (command == "present")
                app->present(wm.state());
              else if (command == "close")
                app->close();
              else if (command == "state") {
              } else
                throw std::runtime_error("unknown app test operation");
            }
            result = app->state();
          } else if (op == "test_storage_layout")
            result = storage_layout(r.at("args").at("windows"));
          else if (op == "test_desktop") {
            if (!desktop)
              desktop = std::make_unique<Desktop>(wm);
            result = desktop->action(r.at("args"), r.at("args").value("now", 0.));
          } else if (op == "test_render") {
            if (!mock) {
              mock = std::make_unique<MockDevice>();
              projectors = std::make_unique<Projectors>(*mock);
              compositor = std::make_unique<Compositor>(geometry, *projectors);
              output = std::make_unique<Output>(*mock, geometry);
              wm.pose = [&](double y, double p) { output->set_pose(y, p); };
            }
            auto c = compositor->update(wm, r.at("args").value("full", false));
            std::string p = r.at("args").at("path");
            write_bytes(p + ".scene", compositor->scene.data(), compositor->scene.size() * 4);
            write_bytes(p + ".update", c.data().data(), c.data().size() * 4);
            try {
              if (c.patch)
                output->patch(c.data());
              else
                output->full(c.data());
              if (!c.patch || !c.data().empty())
                compositor->ack();
            } catch (...) {
              compositor->invalidate();
              throw;
            }
            result = {{"projection", compositor->metrics},
                      {"output", output->status()},
                      {"trace", mock->trace}};
            mock->trace = J::array();
          } else
            result = wm.dispatch(r);
          std::cout << J({{"id", id}, {"ok", true}, {"result", result}}).dump() << std::endl;
        } catch (const std::exception &e) {
          std::cout << J({{"id", id}, {"ok", false}, {"error", e.what()}}).dump() << std::endl;
        }
      }
      return 0;
    }
    std::cerr << "BoAYO native: --rpc (comparison harness)\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
