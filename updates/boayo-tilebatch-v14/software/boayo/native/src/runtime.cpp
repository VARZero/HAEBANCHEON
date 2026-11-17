#include "assets.hpp"
#include "profile.hpp"
#include "driver_profile.hpp"
#include "frame_pacer.hpp"
#include "runtime.hpp"
#include "frame_trace.hpp"
#include "resident.hpp"
#include "apps.hpp"
#include "motion.hpp"
#include "view_playback.hpp"
#include "backend.hpp"
#include "rgb_fill.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <iostream>
#include <cerrno>
#include <cstring>
namespace boayo {
int selftest(bool dual);
static volatile sig_atomic_t interrupted = 0;
static void on_signal(int) {
  interrupted = 1;
}
static double seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string socket_path() {
  const char *p = getenv("BOAYO_SOCKET");
  return p ? p : "/tmp/bosio-wm.sock";
}
struct FD {
  int fd = -1;
  FD() = default;
  explicit FD(int f) : fd(f) {}
  ~FD() {
    if (fd >= 0)
      ::close(fd);
  }
  FD(const FD &) = delete;
  FD &operator=(const FD &) = delete;
};
static sockaddr_un address(const std::string &path) {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  require(path.size() < sizeof(a.sun_path), "socket path too long");
  std::copy(path.begin(), path.end(), a.sun_path);
  return a;
}
struct Client {
  FD sock;
  std::string app, buffer;
  unsigned sequence = 0;
  explicit Client(std::string name = "native-control", std::string path = socket_path())
      : sock(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)),
        app(std::move(name) + ":" + std::to_string(getpid())) {
    require(sock.fd >= 0, "socket creation");
    auto a = address(path);
    require(connect(sock.fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0,
            "connect WM: " + std::string(strerror(errno)));
    timeval timeout{120, 0};
    setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }
  J call(const std::string &op, J args = J::object()) {
    J r = {{"id", ++sequence}, {"app", app}, {"op", op}, {"args", args}};
    std::string s = r.dump() + "\n";
    size_t off = 0;
    while (off < s.size()) {
      auto n = send(sock.fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
      if (n < 0 && errno == EINTR)
        continue;
      require(n > 0, "WM write failed");
      off += n;
    }
    while (buffer.find('\n') == std::string::npos) {
      char chunk[8192];
      auto n = recv(sock.fd, chunk, sizeof(chunk), 0);
      if (n < 0 && errno == EINTR)
        continue;
      require(n > 0, "WM reply failed or timed out");
      buffer.append(chunk, n);
      require(buffer.size() <= 8 * 1024 * 1024, "WM reply too large");
    }
    auto end = buffer.find('\n');
    auto response = J::parse(buffer.substr(0, end));
    buffer.erase(0, end + 1);
    require(response.at("id") == sequence, "IPC sequence mismatch");
    require(response.at("ok").get<bool>(), response.value("error", std::string("WM error")));
    return response.at("result");
  }
};
struct Work {
  J request;
  std::set<std::string> disconnect;
  std::promise<J> reply;
};
struct Bridge {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::shared_ptr<Work>> queue;
  std::atomic<bool> stop{false};
  std::string stage = "initializing";
  double updated = seconds();
  J cached = {{"runtime", "C++17"},        {"windows", J::array()},   {"output", {{"commits", 0}}},
              {"projection", J::object()}, {"render_error", nullptr}, {"app_error", nullptr}};
  void phase(const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex);
    stage = value;
  }
  void publish(J value) {
    std::lock_guard<std::mutex> lock(mutex);
    cached = std::move(value);
    updated = seconds();
  }
  void publish_delta(const SceneStateCache &scene,const J &fields){
    std::lock_guard<std::mutex> lock(mutex);scene.apply(cached);
    for(auto i=fields.begin();i!=fields.end();++i)update_fields(cached[i.key()],i.value());
    updated=seconds();
  }
  J status() {
    std::lock_guard<std::mutex> lock(mutex);
    J result = cached;
    result["service"] = {{"phase", stage},
                         {"snapshot_age_seconds", seconds() - updated},
                         {"queued_requests", queue.size()},
                         {"state_is_snapshot", true}};
    return result;
  }
  J profile_status(){std::lock_guard<std::mutex> lock(mutex);return {{"projection",cached.value("projection",J::object())},{"output_profile",cached.contains("output")?cached["output"].value("publish_profile",J::object()):J::object()},{"driver_profile",cached.value("driver_profile",J::object())},{"profile",cached.value("profile",J::object())},{"performance",cached.value("performance",J::object())},{"phase",stage},{"snapshot_time",updated},{"render_error",cached.value("render_error",J(nullptr))},{"app_error",cached.value("app_error",J(nullptr))}};}
  J view_cached={{"active",false}};
  void view_publish(J value){std::lock_guard<std::mutex> lock(mutex);view_cached=std::move(value);}
  J view_status(){std::lock_guard<std::mutex> lock(mutex);return view_cached;}
  void mock_delay(bool mock) {
    // Test-only delay cannot be enabled for the physical device.
    const char *text = mock ? getenv("BOAYO_MOCK_RENDER_DELAY_MS") : nullptr;
    if (!text)
      return;
    int ms = std::clamp(std::atoi(text), 0, 30000);
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return stop.load(); });
  }
  J motion_progress() {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"rendered_generation",cached.contains("performance")?
      cached["performance"].value("rendered_generation",uint64_t(0)):uint64_t(0)},
      {"render_error",cached.value("render_error",J(nullptr))},
      {"app_error",cached.value("app_error",J(nullptr))},{"phase",stage}};
  }
};
static void worker(bool mock, Bridge &b) {
  b.phase("geometry");
  Geometry geometry;
  WM wm;
  SceneStateCache state_cache;
  b.phase("device_open");
  std::unique_ptr<Device> dev =
      mock ? std::unique_ptr<Device>(new MockDevice) : std::unique_ptr<Device>(new LinuxDevice);
  RgbFillScope rgb_scope({dev.get(), [](void *d,uint8_t *p,size_t n,
                                      const std::array<uint8_t,3> &c) {
    return static_cast<Device *>(d)->rgb_fill(p,n,c);
  }});
  Projectors projectors(*dev);
  b.phase("compositor_allocation");
  Compositor compositor(geometry, projectors, true);
  b.phase("output_initialization");
  setup_driver_profile(*dev);
  Output output(*dev, geometry);
  // Persistent tile bookkeeping exceeds the RV32 musl worker stack budget.
  // Own it on the heap; this contains no full scene pixel buffer.
  auto resident_state=std::make_unique<ResidentCompositor>(geometry,projectors,output);
  auto &resident=*resident_state;
  const char *renderer=getenv("BOAYO_RENDERER");
  bool use_resident=!renderer || std::string(renderer)!="legacy";
  wm.pose = [&](double y, double p, double r) { output.set_pose(y, p, r); };
  std::cout << "BOAYO_NATIVE_INIT backend=" << (mock ? "MOCK" : "BOSIO_ABI3") << std::endl;
  b.phase("blank_metadata_upload");
  output.initialize_blank();
  std::cout << "BOAYO_BLANK_METADATA_READY words=" << output.shadow.size()
            << " tile_composition=0 projection_jobs=0" << std::endl;
  // Do not acknowledge a compositor frame: the first visible scene must be full.
  b.phase("desktop_initialization");
  Desktop desktop(wm);
  MotionExercise motion(wm,desktop);
  ViewPlayback playback(output,wm);
  double view_report_at=0;
  uint64_t rendered = 0, render_count = 0, full_count = 1, patch_count = 0, no_op_count = 0,
           words = output.shadow.size();
  FramePacer pacer;
  double apps_ms=0;
  double fps = 2, last_tick = 0, started = seconds(), compose_ms = 0,
         upload_ms = 0;
  J error = nullptr, app_error = nullptr;

  std::string scene_owner;
  Bytes scene_base;
  uint64_t scene_generation = 0, last_scene = 0;
  std::unique_ptr<NativeRenderer> external_renderer;
  auto release_scene = [&](const std::string &owner) {
    if (scene_owner != owner)
      return;
    scene_owner.clear();
    scene_base.clear();
    scene_generation = last_scene = 0;
    rendered = 0;
    ++wm.generation;
    compositor.invalidate();
  };
  auto state_fields = [&]() -> J {
    J state=J::object();
      state["output"] = output.status();
      state["output"]["mock_backend"] = mock;
      state["buttons"] = {{"state", 0}, {"available", false}, {"last_event_id", 0}};
      state["render_error"] = error;
      state["app_error"] = app_error;
      state["scene_owner"] = scene_owner.empty() ? J(nullptr) : J(scene_owner);
      state["projection"] = compositor.metrics;
      state["assets"] = asset_status();
      state["runtime"] = "C++17";
      state["view"]={{"yaw",wm.view_yaw},{"pitch",wm.view_pitch},{"roll",wm.view_roll}};
      state["motion"]=motion.state();
      state["profile"] = profile::snapshot();
      state["driver_profile"] = driver_profile_status(*dev);
      state["performance"] = {{"render_count", render_count},
                              {"rendered_generation",rendered},
                              {"full_update_count", full_count},
                              {"patch_update_count", patch_count},
                              {"no_op_update_count", no_op_count},
                              {"update_words", words},
                              {"uptime_seconds", seconds() - started},
                              {"last_compose_ms", compose_ms},
                              {"last_apps_ms",apps_ms},{"frame_interval_ms",pacer.interval*1000},
                              {"frame_pacing","start-to-start"},
                              {"last_upload_ms", upload_ms},
                              {"frame_limit", fps}};
    return state;
  };
  auto dispatch = [&](const J &r) -> J {
    std::string op = r.at("op"), owner = r.value("app", std::string("anonymous"));
    auto args = r.value("args", J::object());
    if(op=="view_play") {
      require(!motion.active && wm.generation==rendered,"wait for an idle, presented scene");
      require(wm.buttons.empty() && !wm.capture,"release pointer buttons before view playback");
      playback.start(args.at("path"),owner,seconds(),args.value("duration",60.));
      playback.start_time=seconds();playback.end_time=playback.start_time+args.value("duration",60.);
      b.view_publish(playback.state());return playback.state();
    }
    if(op=="view_stop"){playback.stop();b.view_publish(playback.state());return playback.state();}
    require(!playback.active || op=="get_state" || op=="view_state","stop view playback before modifying windows/input");
    if (op == "motion_start") return motion.start(owner,seconds());
    if (op == "motion_step") return motion.step(owner,args.at("step"),rendered,seconds());
    if (op == "motion_stop") {
      if(args.value("owned",false))require(!motion.active || motion.owner==owner,"motion stop ownership mismatch");
      return motion.stop(seconds());
    }
    if (op == "sync_scene") return wm.generation;
    if (op == "claim_scene") {
      require(scene_owner.empty() || scene_owner == owner,
              "BOSIO scene is owned by another application");
      scene_owner = owner;
      compositor.invalidate();
      return {{"owner", owner}, {"m", 32}};
    }
    if (op == "upload_scene_words") {
      require(scene_owner == owner, "application does not own the BOSIO scene");
      auto raw = unbase64(args.at("words32"));
      require(raw.size() % 4 == 0, "packed scene is not uint32 aligned");
      Words packed(raw.size() / 4);
      std::memcpy(packed.data(), raw.data(), raw.size());
      scene_base = unpack_scene(packed);
      ++scene_generation;
      compositor.invalidate();
      if (wm.windows.empty())
        output.full(packed);
      words += packed.size();
      ++full_count;
      return {{"words", packed.size()}};
    }
    if (op == "release_scene") {
      release_scene(owner);
      return true;
    }
    if (op == "ping")
      return {{"version", 2},
              {"m", 32},
              {"headless", mock},
              {"features",
               {"windows", "pointer", "input-injection", "button-events", "scene-stream",
                "spatial-workspace", "pointer-capture"}},
              {"runtime", "C++17"},
              {"mock_backend", mock}};
    if (op == "get_button_state")
      return {{"state", 0}, {"available", false}, {"last_event_id", 0}};
    if (op == "poll_button_events")
      return {{"events", J::array()},
              {"next_id", std::max(0, args.value("after_id", 0))},
              {"state", 0},
              {"available", false}};
    if (op == "set_frame_limit") {
      double f = args.at("fps");
      require(f >= 1 && f <= 500, "fps must be between 1 and 500");
      fps = f;
      return fps;
    }
    if (op == "set_antialias") {
      require(!args.value("enabled", true), "Linux ABI3 does not expose antialias control");
      return {{"enabled", false},
              {"threshold", args.value("threshold", 24)},
              {"strength", args.value("strength", 32)}};
    }
    if (op == "reset_performance") {
      render_count = full_count = patch_count = no_op_count = words = 0;
      started = seconds();
      return true;
    }
    if (op == "get_state") {J state=state_cache.state(wm);state.update(state_fields());return state;}
    if (op == "launch_app")
      return desktop.launch(args.at("kind"), args.value("azimuth", 0.),
                            args.value("elevation", 0.));
    if (op == "show_launcher") {
      desktop.show(args.value("azimuth", wm.view_yaw), args.value("elevation", wm.view_pitch));
      return true;
    }
    if (op == "shutdown") {
      b.stop = true;
      return true;
    }
    return wm.dispatch(r);
  };

  auto publish = [&] { profile::Scope timing(profile::state_publish);state_cache.state(wm);b.publish_delta(state_cache,state_fields()); };
  publish();
  b.phase("ready");
  std::cout << "BOAYO_NATIVE_READY blank_committed_launcher_pending" << std::endl;
  while (!b.stop) {
    std::deque<std::shared_ptr<Work>> work;
    {
      std::unique_lock<std::mutex> lock(b.mutex);
      b.cv.wait_for(lock, std::chrono::milliseconds(20),
                    [&] { return b.stop || !b.queue.empty(); });
      work.swap(b.queue);
    }
    for (auto &job : work) {
      try {
        b.phase("command");
        if (!job->disconnect.empty()) {
          for (const auto &owner : job->disconnect) {
            if(playback.active && playback.owner==owner){playback.stop();b.view_publish(playback.state());}
            if(motion.active && motion.owner==owner) motion.stop(seconds());
            wm.disconnect(owner);
            release_scene(owner);
          }
          publish();
        } else {
          J result = dispatch(job->request);
          if(!playback.active)publish();
          job->reply.set_value(std::move(result));
        }
      } catch (...) {
        if (job->disconnect.empty())
          job->reply.set_exception(std::current_exception());
      }
      b.phase("ready");
    }
    if (b.stop)
      break;
    double now = seconds();
    if(playback.active) {
      b.phase("view-playback");
      try{playback.tick(now,seconds);}catch(const std::exception&e){error=e.what();}
      // A small progress record only; no window snapshot or pointer warp per frame.
      if(!playback.active || now-view_report_at>=.5){b.view_publish(playback.state());view_report_at=now;}
      if(playback.active)continue;
      publish();b.phase("ready");
    }
    if (now - last_tick >= 1. / 30) {
      b.phase("apps");
      const auto app_start=seconds();
      try {
        desktop.update(now);
        app_error = nullptr;
      } catch (const std::exception &e) {
        app_error = e.what();
        std::cerr << "BOAYO_APP_ERROR " << e.what() << std::endl;
      }
      apps_ms=(seconds()-app_start)*1000;
      last_tick = now;
      b.phase("ready");
    }
    now=seconds(); // Apps may have consumed the entire frame period.
    if ((wm.generation != rendered || scene_generation != last_scene) &&
        pacer.due(now,fps)) {
      pacer.begin(now);
      uint64_t gen = wm.generation;
      try {
        frame_trace::Scope frame_scope(render_count==0);
        b.phase("composing");
        if (!render_count) std::cout << "BOAYO_FIRST_FRAME_COMPOSE_BEGIN" << std::endl;
        b.mock_delay(mock);
        auto start = seconds();
        if(use_resident) {
          require(scene_owner.empty() && ResidentCompositor::supported(wm),"resident renderer needs undecorated windows below 160 degrees; explicit BOAYO_RENDERER=legacy supports other modes");
          bool changed=resident.update(wm);compositor.metrics=resident.metrics;
          compose_ms=(seconds()-start)*1000;upload_ms=0;
          if(changed)++patch_count;else ++no_op_count;
          if(!render_count)std::cout << "BOAYO_FIRST_FRAME_PRESENT_DONE" << std::endl;
          ++render_count;rendered=gen;last_scene=scene_generation;
        } else {
        resident.invalidate();
        Composition c;
        if (!scene_owner.empty() && !scene_base.empty() && !wm.windows.empty()) {
          if (!external_renderer)
            external_renderer = std::make_unique<NativeRenderer>(geometry);
          auto overlay = external_renderer->rgb(wm);
          Bytes rgb = scene_base;
          for (size_t k = 0; k < rgb.size(); k += 3)
            if (overlay[k] || overlay[k + 1] || overlay[k + 2])
              std::copy_n(overlay.data() + k, 3, rgb.data() + k);
          c.words = pack_rgb(rgb);
          compositor.invalidate();
        } else
          c = compositor.update(wm);
        auto middle = seconds();
        b.phase("uploading");
        if (!render_count) std::cout << "BOAYO_FIRST_FRAME_UPLOAD_BEGIN" << std::endl;
        frame_trace::mark("output.upload.begin");
        if (c.patch) {
          output.patch(c.data());
          if (c.data().empty())
            ++no_op_count;
          else {
            compositor.ack();
            ++patch_count;
          }
        } else {
          output.full(c.data(), output.blank_only && c.scene_view != nullptr);
          compositor.ack();
          ++full_count;
        }
        frame_trace::mark("output.upload.end");
        compose_ms = (middle - start) * 1000;
        upload_ms = (seconds() - middle) * 1000;
        words += c.data().size();
        if (!render_count) std::cout << "BOAYO_FIRST_FRAME_PRESENT_DONE" << std::endl;
        ++render_count;
        rendered = gen;
        last_scene = scene_generation;
        }
        error = nullptr;
      } catch (const std::exception &e) {
        compositor.invalidate();
        error = e.what();
        std::cerr << "BOSIO_RENDER_ERROR " << e.what() << std::endl;
      }
      // No end-of-work sleep: processing time already counts toward the limit.
      publish();
      b.phase("ready");
    }
  }
  b.phase("stopping");
  dev->stop();
}
struct Pending {
  J id;
  std::future<J> result;
};
struct Peer {
  explicit Peer(int socket) : fd(socket) {}
  Peer(const Peer &) = delete;
  Peer &operator=(const Peer &) = delete;
  Peer(Peer &&) noexcept = default;
  Peer &operator=(Peer &&) noexcept = default;
  int fd;
  std::string input, output;
  std::set<std::string> owners;
  std::deque<Pending> pending;
};
static int serve(bool mock, const std::string &path) {
  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);
  signal(SIGPIPE, SIG_IGN);
  FD lock(open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
  require(lock.fd >= 0 && flock(lock.fd, LOCK_EX | LOCK_NB) == 0, "WM already running");
  struct stat st {};
  require(lstat(path.c_str(), &st) != 0 && errno == ENOENT,
          "WM socket already exists; inspect before starting");
  FD server(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
  require(server.fd >= 0, "server socket creation");
  auto a = address(path);
  require(bind(server.fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0, "socket bind failed");
  chmod(path.c_str(), 0600);
  require(listen(server.fd, 16) == 0, "socket listen failed");
  Bridge b;
  b.cached["output"]["mock_backend"] = mock;
  std::thread render_thread([&] {
    try {
      worker(mock, b);
    } catch (const std::exception &e) {
      {
        std::lock_guard<std::mutex> lock(b.mutex);
        b.cached["render_error"] = e.what();
      }
      b.phase("failed");
      std::cerr << "BOAYO_WORKER_ERROR " << e.what() << std::endl;
      std::lock_guard<std::mutex> lock(b.mutex);
      for (auto &job : b.queue)
        if (job->disconnect.empty())
          job->reply.set_exception(std::current_exception());
      b.queue.clear();
    }
  });
  std::cout << "BOAYO_NATIVE_SERVICE " << path << " initializing; query boayo-status" << std::endl;
  std::vector<Peer> peers;
  auto submit = [&](const J &r) -> std::future<J> {
    auto job = std::make_shared<Work>();
    auto future = job->reply.get_future();
    std::string op = r.at("op");
    auto args = r.value("args", J::object());
    if (op == "get_state")
      job->reply.set_value(b.status());
    else if (op == "profile_progress")
      job->reply.set_value(b.profile_status());
    else if (op == "view_progress")
      job->reply.set_value(b.view_status());
    else if (op == "motion_progress")
      job->reply.set_value(b.motion_progress());
    else if (op == "ping")
      job->reply.set_value({{"version", 2},
                            {"m", 32},
                            {"headless", mock},
                            {"mock_backend", mock},
                            {"runtime", "C++17"},
                            {"features",
                             {"windows", "pointer", "input-injection", "button-events",
                              "scene-stream", "spatial-workspace", "pointer-capture"}}});
    else if (op == "get_button_state")
      job->reply.set_value({{"state", 0}, {"available", false}, {"last_event_id", 0}});
    else if (op == "poll_button_events")
      job->reply.set_value({{"events", J::array()},
                            {"next_id", std::max(0, args.value("after_id", 0))},
                            {"state", 0},
                            {"available", false}});
    else if (op == "shutdown") {
      b.stop = true;
      b.cv.notify_all();
      job->reply.set_value(true);
    } else {
      job->request = r;
      std::lock_guard<std::mutex> guard(b.mutex);
      require(b.stage != "failed", "render worker failed; read boayo-status");
      require(b.queue.size() < 256, "command queue full");
      b.queue.push_back(job);
      b.cv.notify_one();
    }
    return future;
  };
  auto cleanup = [&] {
    b.stop = true;
    b.cv.notify_all();
    render_thread.join();
    for (auto &p : peers)
      close(p.fd);
    unlink(path.c_str());
  };
  try {
    while (!b.stop && !interrupted) {
      std::vector<pollfd> fds{{server.fd, POLLIN, 0}};
      for (auto &p : peers)
        fds.push_back({p.fd, short(POLLIN | (p.output.empty() ? 0 : POLLOUT)), 0});
      int n = poll(fds.data(), fds.size(), 10);
      if (n < 0 && errno == EINTR)
        continue;
      require(n >= 0, "server poll failed");
      if (fds[0].revents & POLLIN) {
        int fd = accept4(server.fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd >= 0) {
          if (peers.size() < 64)
            peers.emplace_back(fd);
          else
            close(fd);
        }
      }
      for (size_t i = 0; i + 1 < fds.size(); i++) {
        auto &p = peers[i];
        bool dead = fds[i + 1].revents & (POLLERR | POLLNVAL);
        if (fds[i + 1].revents & (POLLIN | POLLHUP)) {
          char buf[16384];
          auto size = recv(p.fd, buf, sizeof(buf), 0);
          if (size > 0)
            p.input.append(buf, size);
          else if (size == 0 || (errno != EAGAIN && errno != EINTR))
            dead = true;
        }
        if (p.input.size() > 8 * 1024 * 1024)
          dead = true;
        // One outstanding operation per peer preserves that client's ordering.
        // Other connections can always query the cached state while it executes.
        if (!dead && p.pending.empty() && p.input.find('\n') != std::string::npos) {
          size_t pos = p.input.find('\n');
          std::string line = p.input.substr(0, pos);
          p.input.erase(0, pos + 1);
          J id = nullptr;
          try {
            auto r = J::parse(line);
            id = r.value("id", J(nullptr));
            p.owners.insert(r.value("app", std::string("anonymous")));
            p.pending.push_back({id, submit(r)});
          } catch (const std::exception &e) {
            p.output += J({{"id", id}, {"ok", false}, {"error", e.what()}}).dump() + "\n";
          }
        }
        if (!p.pending.empty() && p.pending.front().result.wait_for(std::chrono::seconds(0)) ==
                                      std::future_status::ready) {
          auto &pending = p.pending.front();
          J response;
          try {
            response = {{"id", pending.id}, {"ok", true}, {"result", pending.result.get()}};
          } catch (const std::exception &e) {
            response = {{"id", pending.id}, {"ok", false}, {"error", e.what()}};
          }
          p.output += response.dump() + "\n";
          p.pending.pop_front();
        }
        if (p.output.size() > 16 * 1024 * 1024)
          dead = true;
        if (!dead && !p.output.empty()) {
          auto size = send(p.fd, p.output.data(), p.output.size(), MSG_NOSIGNAL);
          if (size > 0)
            p.output.erase(0, size);
          else if (size < 0 && errno != EAGAIN && errno != EINTR)
            dead = true;
        }
        if (dead) {
          auto job = std::make_shared<Work>();
          job->disconnect = std::move(p.owners);
          if (!job->disconnect.empty()) {
            std::lock_guard<std::mutex> guard(b.mutex);
            if (b.stage != "failed")
              b.queue.push_back(job);
          }
          close(p.fd);
          p.fd = -1;
        }
      }
      peers.erase(std::remove_if(peers.begin(), peers.end(), [](auto &p) { return p.fd < 0; }),
                  peers.end());
    }
  } catch (...) {
    cleanup();
    throw;
  }
  cleanup();
  return 0;
}
struct Terminal {
  termios original{};
  bool enabled = false;
  Terminal() {
    require(isatty(STDIN_FILENO), "boayo-keys requires an interactive terminal");
    require(tcgetattr(STDIN_FILENO, &original) == 0, "terminal read failed");
    auto t = original;
    t.c_lflag &= ~(ECHO | ICANON);
    t.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    require(tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0, "terminal setup failed");
    enabled = true;
  }
  ~Terminal() {
    if (enabled)
      tcsetattr(STDIN_FILENO, TCSANOW, &original);
  }
};
static int keys(double step) {
  require(std::isfinite(step) && step >= .1 && step <= 30, "step must be 0.1..30 degrees");
  Client c("uart-keyboard");
  J pose = c.call("view_state");
  Terminal terminal;
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  std::cout
      << "BOAYO_KEYS_READY WASD/arrows=view Z/C=roll Space/Enter=center-click R=reset +/-=step Q=exit\n"
      << pose.dump() << std::endl;
  std::string escape;
  bool last_cr = false;
  while (!interrupted) {
    pollfd fd{0, POLLIN, 0};
    if (poll(&fd, 1, 200) <= 0) {
      escape.clear();
      continue;
    }
    char buf[32];
    auto n = read(0, buf, sizeof(buf));
    if (n <= 0)
      break;
    for (int i = 0; i < n; i++) {
      char key = std::tolower(static_cast<unsigned char>(buf[i]));
      if (!escape.empty()) {
        escape += buf[i];
        if (escape == "\x1b[" || escape == "\x1bO")
          continue;
        if (escape.size() == 3 && (escape[1] == '[' || escape[1] == 'O')) {
          char a = escape[2];
          key = a == 'A' ? 'w' : a == 'B' ? 's' : a == 'C' ? 'd' : a == 'D' ? 'a' : 0;
        } else
          key = 0;
        escape.clear();
      } else if (buf[i] == 27) {
        escape = "\x1b";
        continue;
      }
      if (key == '\n' && last_cr) {
        last_cr = false;
        continue;
      }
      last_cr = key == '\r';
      if (key == 'q' || key == 3 || key == 4) {
        interrupted = 1;
        break;
      }
      J result;
      switch (key) {
      case 'w':
        result = c.call("view_move", {{"yaw", 0}, {"pitch", step}});
        break;
      case 's':
        result = c.call("view_move", {{"yaw", 0}, {"pitch", -step}});
        break;
      case 'a':
        result = c.call("view_move", {{"yaw", -step}, {"pitch", 0}});
        break;
      case 'd':
        result = c.call("view_move", {{"yaw", step}, {"pitch", 0}});
        break;
      case 'z':
        result = c.call("view_move", {{"roll", -step}});
        break;
      case 'c':
        result = c.call("view_move", {{"roll", step}});
        break;
      case ' ':
      case '\r':
      case '\n':
        result = c.call("view_click");
        break;
      case 'r':
        result = c.call("view_reset");
        break;
      case '+':
      case '=':
        step = std::min(30., step * 2);
        result = {{"step", step}};
        break;
      case '-':
        step = std::max(.1, step / 2);
        result = {{"step", step}};
        break;
      default:
        break;
      }
      if (!result.is_null())
        std::cout << result.dump() << std::endl;
    }
  }
  std::cout << "\nBOAYO_KEYS_EXIT (WM remains running)" << std::endl;
  return 0;
}
static int motion_cli(int loops,int hold_ms,int max_steps=MotionExercise::steps) {
  require(max_steps>=1 && max_steps<=MotionExercise::steps,"motion steps must be 1..25");
  const bool trace=getenv("BOAYO_MOTION_PROFILE")!=nullptr;
  require(loops>=1 && loops<=100,"motion loops must be 1..100");
  require(hold_ms>=0 && hold_ms<=10000,"motion hold must be 0..10000 ms");
  Client c("motion-exercise");signal(SIGINT,on_signal);signal(SIGTERM,on_signal);
  interrupted=0;bool owned=false;
  auto wait=[&](uint64_t target,bool cancellable=true){
    double deadline=seconds()+900;
    for(;;){
      if(cancellable && interrupted)return false;
      J state=c.call("motion_progress");
      if(!state["render_error"].is_null() || !state["app_error"].is_null())
        throw std::runtime_error("motion render/app error: "+state.dump());
      if(state["rendered_generation"].get<uint64_t>()>=target)return true;
      require(seconds()<deadline,"motion output completion timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  };
  try{
    for(int loop=0;loop<loops && !interrupted;++loop){
      auto r=c.call("motion_start");owned=true;
      std::cout<<"BOAYO_MOTION_BEGIN loop="<<loop+1<<" steps="<<max_steps<<std::endl;
      if(!wait(r["target_generation"]))break;
      for(int i=0;i<max_steps && !interrupted;++i){
        double start=seconds();r=c.call("motion_step",{{"step",i}});
        if(!wait(r["target_generation"]))break;
        std::cout<<"BOAYO_MOTION_STEP "<<J({{"step",i+1},{"phase",r["phase"]},{"view",r["view"]},{"seconds",seconds()-start},{"output_complete",true}}).dump()<<std::endl;
        if(trace && i>=9)std::cout<<"BOAYO_MOTION_PROFILE "<<J({{"step",i+1},{"counters",c.call("profile_progress")}}).dump()<<std::endl;
        double end=seconds()+hold_ms/1000.;while(!interrupted && seconds()<end)std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      auto end=c.call("motion_stop",{{"owned",true}});owned=false;wait(end["target_generation"],false);
    }
    if(owned){auto end=c.call("motion_stop",{{"owned",true}});owned=false;wait(end["target_generation"],false);}
    std::cout<<(interrupted?"BOAYO_MOTION_STOPPED restored":"BOAYO_MOTION_COMPLETE device-acknowledged; HDMI visual check separate")<<std::endl;
    return interrupted?130:0;
  }catch(...){if(owned){try{c.call("motion_stop",{{"owned",true}});}catch(...){}}throw;}
}
int runtime_main(int argc, char **argv) {
  std::string command = argc > 1 ? argv[1] : "help";
  bool mock = false;
  for (int i = 2; i < argc; i++)
    if (std::string(argv[i]) == "--mock" || std::string(argv[i]) == "--headless")
      mock = true;
  if (command == "serve")
    return serve(mock, socket_path());
  if (command == "selftest" || command == "dual-selftest")
    return selftest(command == "dual-selftest");
  if (command == "start") {
    struct stat s {};
    require(lstat(socket_path().c_str(), &s) != 0 && errno == ENOENT,
            "WM socket exists; inspect boayo-status first");
    FD lock(open("/tmp/boayo-control.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600));
    require(lock.fd >= 0 && flock(lock.fd, LOCK_EX | LOCK_NB) == 0, "another start is in progress");
    pid_t child = fork();
    require(child >= 0, "fork failed");
    if (!child) {
      setsid();
      int log = open("/tmp/boayo-wm.log", O_CREAT | O_WRONLY | O_APPEND, 0600),
          nullfd = open("/dev/null", O_RDONLY);
      if (log < 0 || nullfd < 0)
        _exit(126);
      dup2(nullfd, 0);
      dup2(log, 1);
      dup2(log, 2);
      close(log);
      close(nullfd);
      close(lock.fd);
      lock.fd = -1;
      try {
        _exit(serve(mock, socket_path()));
      } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        _exit(127);
      }
    }
    double end = seconds() + 900;
    while (seconds() < end) {
      int status = 0;
      if (waitpid(child, &status, WNOHANG) == child)
        throw std::runtime_error("WM startup failed; read /tmp/boayo-wm.log");
      if (lstat(socket_path().c_str(), &s) == 0) {
        Client c;
        auto p = c.call("ping");
        std::cout << "BoAYO C++ service started; initialization continues. boayo-status / "
                     "boayo-keys / boayo-stop\n"
                  << p.dump() << std::endl;
        return 0;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    kill(child, SIGTERM);
    throw std::runtime_error("WM startup timed out");
  }
  if(command=="view-play") {
    require(argc>=3,"view-play path [seconds]");Client c("view-playback");
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);interrupted=0;bool owned=false;
    try {
      auto r=c.call("view_play",{{"path",argv[2]},{"duration",argc>3?std::stod(argv[3]):60.}});
      owned=true;
      std::cout<<"BOAYO_VIEW_BEGIN "<<r.dump()<<std::endl;
      while(!interrupted){std::this_thread::sleep_for(std::chrono::seconds(1));r=c.call("view_progress");if(!r.value("active",false))break;}
      if(interrupted)r=c.call("view_stop");
      std::cout<<"BOAYO_VIEW_END "<<r.dump()<<std::endl;return r.value("error",std::string()).empty()?0:1;
    }catch(...){if(owned){try{c.call("view_stop");}catch(...){}}throw;}
  }
  if (command == "motion")
    return motion_cli(argc>2?std::stoi(argv[2]):1,argc>3?std::stoi(argv[3]):500,argc>4?std::stoi(argv[4]):MotionExercise::steps);
  if (command == "keys")
    return keys(argc > 2 ? std::stod(argv[2]) : 2.);
  if (command == "help") {
    std::cout
        << "boayo-native start [--mock] | stop | status | keys [degrees] | view yaw pitch [roll] | view-set yaw pitch roll | view-play path [seconds] | view-stop | view-status | motion [loops] [hold_ms] [steps] | motion-stop | move "
           "yaw pitch | click | center-click | app dashboard|telemetry|pulse | launcher\n";
    return 0;
  }
  Client c;
  if (command == "stop")
    c.call("shutdown");
  else if(command=="view-stop")std::cout<<c.call("view_stop").dump()<<std::endl;
  else if(command=="profile")std::cout<<c.call("profile_progress").dump()<<std::endl;
  else if(command=="view-status")std::cout<<c.call("view_progress").dump()<<std::endl;
  else if (command == "motion-stop")
    std::cout << c.call("motion_stop").dump() << std::endl;
  else if (command == "status")
    std::cout << c.call("get_state").dump(2) << std::endl;
  else if (command == "view" || command == "view-set" || command == "move") {
    require(argc == 4 || (argc==5 && command!="move"), "yaw pitch [roll] required");
    double y=finite(std::stod(argv[2])),p=finite(std::stod(argv[3]));
    J args=command=="move"?J{{"delta_azimuth",y},{"delta_elevation",p}}:J{{"yaw",y},{"pitch",p}};
    if(argc==5)args["roll"]=finite(std::stod(argv[4]));
    std::cout << c.call(command=="move"?"pointer_move":command=="view-set"?"view_set":"view_move",args).dump()<<std::endl;
  } else if (command == "click")
    std::cout << c.call("input_click", {{"button", "left"}}).dump() << std::endl;
  else if (command == "center-click")
    std::cout << c.call("view_click").dump() << std::endl;
  else if (command == "app") {
    require(argc == 3, "app requires dashboard, telemetry, or pulse");
    std::cout << "BOAYO_APP_READY window=" << c.call("launch_app", {{"kind", argv[2]}}).dump()
              << std::endl;
  } else if (command == "launcher")
    c.call("show_launcher");
  else
    throw std::runtime_error("unknown command");
  return 0;
}
} // namespace boayo
