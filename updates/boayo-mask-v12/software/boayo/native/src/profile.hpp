#pragma once
#include "common.hpp"
#include <time.h>
namespace boayo::profile {
// Worker-local cumulative counters; no per-frame console output or pixel audit.
enum Id { apps, app_present, image, publish, captions, workspace, compose,
          layers, pointer, tiles_prepare, texture, descriptors, projection,
          blend, output, finish, state_publish, count };
inline constexpr const char *names[]={"apps","app_present","image","image_publish",
  "captions","workspace","compose","layers","pointer","tiles_prepare",
  "texture","descriptors","projection_wait","blend","output_publish","finish","state_publish"};
struct Counter { uint64_t wall_ns=0,cpu_ns=0,calls=0; };
inline thread_local std::array<Counter,count> counters{};
inline bool enabled(){static const bool value=[] {auto p=getenv("BOAYO_PROFILE");return p && std::string(p)=="1";}();return value;}
inline uint64_t clock_ns(clockid_t id){timespec t{};clock_gettime(id,&t);return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);}
struct Scope {
  Id id;uint64_t wall=0,cpu=0;bool active;
  explicit Scope(Id i):id(i),active(enabled()){if(active){wall=clock_ns(CLOCK_MONOTONIC);cpu=clock_ns(CLOCK_THREAD_CPUTIME_ID);}}
  void stop(){if(active){auto end=clock_ns(CLOCK_MONOTONIC),c=clock_ns(CLOCK_THREAD_CPUTIME_ID);auto &v=counters[id];v.wall_ns+=end-wall;v.cpu_ns+=c-cpu;++v.calls;active=false;}}
  void next(Id i){stop();id=i;active=enabled();if(active){wall=clock_ns(CLOCK_MONOTONIC);cpu=clock_ns(CLOCK_THREAD_CPUTIME_ID);}}
  ~Scope(){stop();}
  Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
inline J snapshot(){J result={{"enabled",enabled()},{"units","microseconds"},{"nested",true}};if(enabled())for(unsigned i=0;i<count;++i){auto &v=counters[i];result[names[i]]={v.wall_ns/1000,v.cpu_ns/1000,v.calls};}return result;}
}
