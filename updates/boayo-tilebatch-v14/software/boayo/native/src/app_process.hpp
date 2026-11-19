#pragma once
#include "ipc.hpp"
#include "apps.hpp"
#include <sys/types.h>
namespace boayo {
struct MappedSurface {
  int fd = -1;
  uint8_t *pixels = nullptr;
  size_t slot_bytes = 0;
  uint64_t sequence = 0;
  explicit MappedSurface(const J &description);
  ~MappedSurface();
  MappedSurface(const MappedSurface &) = delete;
  MappedSurface &operator=(const MappedSurface &) = delete;
};
// Application-side WM mirror. Only metadata and damaged RGB rectangles cross IPC.
struct RemoteWM {
  WM wm;
  std::string path;
  std::map<std::string, std::unique_ptr<Client>> clients;
  std::map<int, std::string> owners;
  std::map<int, std::unique_ptr<MappedSurface>> surfaces;
  explicit RemoteWM(std::string socket);
  Client &client(const std::string &owner);
  J call(const J &request);
  void publish(int id, const Surface &surface, bool first);
  J events(const std::string &owner);
  uint64_t last_generation = 0;
};
pid_t spawn_application(const std::string &kind, const std::string &socket, double az, double el);
int application_main(const std::string &kind, const std::string &socket, double az, double el);
int benchmark_shared_surface(const std::string &socket, unsigned rounds);
}
