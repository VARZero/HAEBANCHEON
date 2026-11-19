#include "app_process.hpp"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <spawn.h>
#include <sys/mman.h>
#include <thread>
extern char **environ;
namespace boayo {
static double app_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
MappedSurface::MappedSurface(const J &d) {
  slot_bytes = d.at("slot_bytes");
  fd = open(d.at("path").get<std::string>().c_str(), O_RDWR | O_CLOEXEC);
  require(fd >= 0, "open shared surface failed");
  void *p = mmap(nullptr, slot_bytes*2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    close(fd);
    throw std::runtime_error("map shared surface failed");
  }
  pixels = static_cast<uint8_t *>(p);
}
MappedSurface::~MappedSurface() {
  if (pixels) munmap(pixels, slot_bytes*2);
  if (fd >= 0) close(fd);
}
RemoteWM::RemoteWM(std::string socket) : path(std::move(socket)) {
  wm.remote = [this](const J &r) { return call(r); };
  wm.publish_surface = [this](int id, const Surface &s, bool first) { publish(id, s, first); };
}
Client &RemoteWM::client(const std::string &owner) {
  auto &c = clients[owner];
  if (!c) c = std::make_unique<Client>(owner, path);
  return *c;
}
J RemoteWM::call(const J &r) {
  std::string owner = r.value("app", std::string("application"));
  std::string op = r.at("op");
  auto args = r.value("args", J::object());
  auto result = client(owner).call(op, args);
  if (result.is_object() && result.contains("window_id") && result.contains("surface_width")) {
    int id = result["window_id"];
    wm.windows[id].data = result;
    if (op == "create_window") { owners[id] = owner; wm.order.push_back(id); }
    if (result.value("focused", false)) wm.focus = id;
  }
  if (op == "destroy_window") {
    int id = args.at("window_id");
    surfaces.erase(id); owners.erase(id); wm.windows.erase(id);
    wm.order.erase(std::remove(wm.order.begin(), wm.order.end(), id), wm.order.end());
  }
  if (op == "close_windows") {
    std::vector<int> closed;
    for (auto &[id, name] : owners) if (name == owner) closed.push_back(id);
    for (int id : closed) {
      surfaces.erase(id); owners.erase(id); wm.windows.erase(id);
      wm.order.erase(std::remove(wm.order.begin(), wm.order.end(), id), wm.order.end());
    }
  }
  if (op == "poll_events")
    for (auto &event : result)
      if (event.value("type", "") == "focus") {
        if (event.value("focused", false)) wm.focus = event.at("window_id");
        else if (wm.focus == event.value("window_id", 0)) wm.focus = 0;
      }
  if (op == "capture_pointer") wm.capture = args.value("window_id", 0);
  if (op == "release_pointer") wm.capture = 0;
  if (op == "input_button") {
    std::string button = args.value("button", std::string("left"));
    if (args.at("pressed").get<bool>()) wm.buttons.insert(button);
    else wm.buttons.erase(button);
  }
  if (op == "input_warp" || op == "pointer_warp") {
    wm.paz = args.at("azimuth"); wm.pel = args.at("elevation");
  }
  if (op == "view_set" || op == "view_move" || op == "view_reset") {
    wm.view_yaw = result.at("yaw"); wm.view_pitch = result.at("pitch");
    wm.view_roll = result.value("roll", 0.);
  }
  if (op == "sync_scene") {
    last_generation = result.get<uint64_t>();
    wm.generation = last_generation;
  }
  return result;
}
void RemoteWM::publish(int id, const Surface &s, bool first) {
  auto &w = wm.window(id);
  s.ensure_pixels();
  require(size_t(s.width)*s.height*3 == s.pixels.size(), "invalid application image");
  std::vector<Rect> changed;
  if (first || w.surface.empty()) {
    w.surface = s.pixels;
    changed = {{0, 0, s.width, s.height}};
  } else {
    changed = patch_rgb_tiles(w.surface, s.width, {0, 0, s.width, s.height}, s.pixels);
    changed = tiled_regions(changed, s.width, s.height);
  }
  if (changed.empty()) return;
  auto &c = client(owners.at(id));
  auto &mapping = surfaces[id];
  if (!mapping) mapping = std::make_unique<MappedSurface>(
      c.call("create_surface", {{"window_id", id}}));
  // A slot is reusable as soon as its commit reply acknowledges the snapshot.
  unsigned slot = mapping->sequence % 2;
  auto *target = mapping->pixels + slot*mapping->slot_bytes;
  J damage = J::array();
  if (changed.size() > 128) changed = {{0, 0, s.width, s.height}};
  for (auto r : changed) {
    damage.push_back({r.x, r.y, r.w, r.h});
    for (int y = r.y; y < r.y+r.h; ++y) {
      size_t offset = (size_t(y)*s.width+r.x)*3;
      std::memcpy(target+offset, s.pixels.data()+offset, size_t(r.w)*3);
    }
  }
  auto result = c.call("commit_surface", {{"window_id", id}, {"slot", slot},
      {"sequence", ++mapping->sequence}, {"damage", damage}});
  last_generation = result.at("generation");
  wm.generation = last_generation;
}
J RemoteWM::events(const std::string &owner) {
  auto result = client(owner).call("app_events");
  wm.focus = result.at("focused_window");
  auto view = result.at("view");
  wm.view_yaw = view[0]; wm.view_pitch = view[1]; wm.view_roll = view[2];
  return result;
}
pid_t spawn_application(const std::string &kind, const std::string &socket, double az, double el) {
  std::string a = std::to_string(az), e = std::to_string(el);
  const char *args[] = {"boayo", "--app-process", kind.c_str(), socket.c_str(),
                        a.c_str(), e.c_str(), nullptr};
  pid_t pid = -1;
  int result = posix_spawn(&pid, "/proc/self/exe", nullptr, nullptr,
                           const_cast<char **>(args), environ);
  require(result == 0, "application process creation failed");
  return pid;
}
static void run_launcher(RemoteWM &remote, double az, double el) {
  auto &c = remote.client("launcher");
  J apps = {{{"id", "dashboard"}, {"name", "Dashboard"}, {"color", "#347CFF"}},
            {{"id", "telemetry"}, {"name", "Telemetry"}, {"color", "#33B57A"}},
            {{"id", "sdk-pulse"}, {"name", "SDK Pulse"}, {"color", "#347CFF"}}};
  auto window = remote.wm.dispatch({{"op", "create_window"}, {"app", "launcher"},
      {"args", {{"title", "BoAYO Launcher"}, {"surface_width", 640}, {"surface_height", 360},
        {"azimuth", az}, {"elevation", el}, {"width_deg", 42}, {"height_deg", 30},
        {"always_on_top", true}, {"shape",{{51.2,36.,537.6,252.,18.}}},
        {"workspace", {{"role", "launcher"}}}}}});
  int id = window.at("window_id");
  auto image = launcher_image(apps);
  publish(remote.wm, id, image, true);
  int pressed = -1;
  for (;;) {
    auto events = remote.events("launcher");
    for (auto &event : events.at("events")) {
      if (event.value("type", "") != "pointer_button" || event.value("button", "") != "left")
        continue;
      double x = event.value("u", 0.)*640, y = event.value("v", 0.)*360;
      int row = int((y-58)/54);
      int selected = x >= 305 && x <= 571 && y >= 58 && row < 3 ? row : -1;
      if (event.value("pressed", false)) pressed = selected;
      else if (pressed >= 0 && pressed == selected) {
        c.call("launch_app", {{"kind", apps[pressed]["id"]}});
        c.call("configure_window", {{"window_id", id}, {"mapped", false}});
        pressed = -1;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}
int application_main(const std::string &kind, const std::string &socket, double az, double el) {
  signal(SIGINT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  signal(SIGPIPE, SIG_IGN);
  RemoteWM remote(socket);
  if (kind == "launcher") { run_launcher(remote, az, el); return 0; }
  require(kind == "dashboard" || kind == "telemetry" || kind == "sdk-pulse",
          "unknown application kind");
  App app(remote.wm, "application", kind, az, el);
  double last = -1;
  while (!app.closed) {
    double now = app_seconds();
    auto events = remote.events(app.owner);
    for (auto &event : events.at("events")) app.handle(event, now);
    app.tick(now);
    double period = kind == "telemetry" ? 1./12 : kind == "sdk-pulse" ? 1. : .5;
    if (last < 0 || now-last >= period) {
      if (last >= 0 && kind == "telemetry") ++app.step;
      if (last >= 0 && kind == "sdk-pulse") app.step = (app.step+1)%10;
      app.present_counts(events.at("apps"), events.at("stored"));
      last = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  return 0;
}
int benchmark_shared_surface(const std::string &socket, unsigned rounds) {
  require(rounds >= 1 && rounds <= 1000, "rounds must be 1..1000");
  Client c("shared-surface-benchmark", socket);
  int id = c.call("create_window", {{"title","Shared surface benchmark"},
      {"surface_width",64},{"surface_height",64},{"width_deg",16},{"height_deg",12},
      {"input_enabled",false}}).at("window_id");
  MappedSurface surface(c.call("create_surface", {{"window_id",id}}));
  std::memset(surface.pixels, 32, surface.slot_bytes);
  auto initial = c.call("commit_surface", {{"window_id",id},{"slot",0},{"sequence",1},
      {"damage",{{0,0,64,64}}}});
  c.call("wait_present", {{"generation",initial.at("generation")}});
  uint64_t sequence = 1;
  for (unsigned edge : {1u, 8u}) {
    std::vector<double> samples;
    for (unsigned n = 0; n <= rounds; ++n) {
      unsigned slot = sequence%2;
      for (unsigned y = 9; y < 9+edge; ++y)
        for (unsigned x = 12; x < 12+edge; ++x) {
          auto *pixel = surface.pixels+slot*surface.slot_bytes+(y*64+x)*3;
          pixel[0] = n%2 ? 224 : 64; pixel[1] = 144; pixel[2] = 32;
        }
      double started = app_seconds();
      auto commit = c.call("commit_surface", {{"window_id",id},{"slot",slot},
          {"sequence",++sequence},{"damage",{{12,9,edge,edge}}}});
      auto complete = c.call("wait_present", {{"generation",commit.at("generation")}});
      double elapsed = (app_seconds()-started)*1000;
      require(complete.at("generation").get<uint64_t>() >=
              commit.at("generation").get<uint64_t>(), "presentation acknowledgement mismatch");
      if (n) samples.push_back(elapsed);
    }
    std::sort(samples.begin(), samples.end());
    double p95 = samples[(95*samples.size()+99)/100-1];
    std::cout << "BOAYO_SHARED_BENCH " << J({{"edge",edge},{"rounds",rounds},
        {"median_ms",samples[samples.size()/2]},{"p95_ms",p95},
        {"target_ms",300},{"target_met",p95<=300},{"includes","commit IPC to output acknowledgement"}})
        .dump() << std::endl;
  }
  c.call("destroy_window", {{"window_id",id}});
  return 0;
}
}
