#pragma once
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

// Diagnostic only: bounded first-desktop trace, no pixel/geometry changes.
// Integer clocks avoid adding software floating point to the measured path.
namespace boayo::init_trace {
inline thread_local bool active = false;
inline thread_local unsigned sequence = 0;
inline thread_local long long wall_start = 0, cpu_start = 0;
inline thread_local int uart = -1;
inline long long clock_us(clockid_t kind) {
  timespec t{};
  if (clock_gettime(kind, &t)) return -1;
  return static_cast<long long>(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
inline void mark(const char *event, long long value = 0) {
  if (!active || sequence >= 128) return;
  const int saved_errno = errno;
  long long wall = clock_us(CLOCK_MONOTONIC), cpu = clock_us(CLOCK_THREAD_CPUTIME_ID);
  char line[224];
  int n = std::snprintf(line, sizeof(line),
      "BOAYO_INITTRACE n=%u event=%s wall_us=%lld cpu_us=%lld value=%lld\n",
      ++sequence, event, wall < 0 ? -1 : wall-wall_start,
      cpu < 0 || cpu_start < 0 ? -1 : cpu-cpu_start, value);
  if (n > 0 && n < static_cast<int>(sizeof(line))) {
    // stderr is the persistent /tmp/boayo-wm.log in daemon mode.
    (void)!write(STDERR_FILENO, line, n);
    if (uart >= 0) (void)!write(uart, line, n);
  }
  errno = saved_errno;
}
struct Scope {
  bool owner = false;
  Scope() {
    const char *enabled = std::getenv("BOAYO_INIT_TRACE");
    if (active || !enabled || std::strcmp(enabled, "1")) return;
    owner = active = true; sequence = 0;
    wall_start = clock_us(CLOCK_MONOTONIC); cpu_start = clock_us(CLOCK_THREAD_CPUTIME_ID);
    const char *console = std::getenv("BOAYO_INIT_TRACE_UART");
    if (console && !std::strcmp(console, "1")) uart = open("/dev/console", O_WRONLY | O_CLOEXEC);
    mark("desktop.begin");
  }
  ~Scope() {
    if (!owner) return;
    mark("desktop.scope_exit");
    if (uart >= 0) close(uart);
    uart = -1; active = false;
  }
};
}
