#pragma once
#include "common.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
namespace boayo {
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
}
