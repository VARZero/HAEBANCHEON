#pragma once
#include "backend.hpp"
#include "profile.hpp"
#include <sys/ioctl.h>
#include <cerrno>
namespace boayo {
#if defined(BO_PROFILE_CONTROL) && defined(BO_GET_PROFILE)
inline thread_local bool driver_diag_available=false,driver_fast=true;
inline void setup_driver_profile(Device &device){
  auto *linux=dynamic_cast<LinuxDevice *>(&device);if(!linux)return;
  const char *legacy=getenv("BOAYO_DRIVER_LEGACY");driver_fast=!(legacy && std::string(legacy)=="1");
  bo_profile_control control{profile::enabled()?1u:0u,driver_fast?1u:0u,1,0};
  if(ioctl(linux->fd,BO_PROFILE_CONTROL,&control)==0)driver_diag_available=true;
  else require(errno==ENOTTY,"driver profiling setup failed");
}
inline J driver_profile_status(Device &device){
  J result={{"available",driver_diag_available},{"enabled",driver_diag_available && profile::enabled()}};
  if(!driver_diag_available)return result;
  result["fast_paths"]=driver_fast;
  if(!profile::enabled())return result;
  auto *linux=dynamic_cast<LinuxDevice *>(&device);bo_driver_profile p{};
  require(linux && ioctl(linux->fd,BO_GET_PROFILE,&p)==0,"driver profiling read failed");
  result["version"]=p.version;result["units"]="microseconds";
#define BO_JSON_DIAG(name) result[#name]=p.name;
  BO_DIAG_FIELDS(BO_JSON_DIAG)
#undef BO_JSON_DIAG
  return result;
}
#else
// The stock ABI3 board driver does not expose the v14 diagnostic extensions.
inline void setup_driver_profile(Device &) {}
inline J driver_profile_status(Device &) {
  return {{"available",false},{"enabled",false}};
}
#endif
}
