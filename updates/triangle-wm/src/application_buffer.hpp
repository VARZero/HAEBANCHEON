#pragma once
#include "dirty_regions.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace boayo {
// The client owns a slot until commit_surface. The reply releases that slot
// after all declared rectangles have been copied into WM-owned storage.
struct ApplicationBuffer {
  struct Patch { Rect area; Bytes rgb; };
  int fd = -1;
  uint8_t *mapping = nullptr;
  unsigned width, height, pixel_bytes;
  std::string format;
  size_t slot_bytes;
  Bytes latest; // Authoritative packed pixels; independent of released app slots.
  uint64_t sequence = 0, commits = 0, copied_bytes = 0;

  ApplicationBuffer(unsigned w, unsigned h, std::string fmt, uint32_t background) : width(w), height(h), pixel_bytes(fmt=="XRGB8888"?4:fmt=="RGB332"?1:3), format(std::move(fmt)), slot_bytes(size_t(w)*h*pixel_bytes) {
    require(format=="RGB24" || format=="XRGB8888" || format=="RGB332", "unsupported surface format");
    latest.resize(slot_bytes);
    for(size_t i=0;i<size_t(width)*height;i++) {
      auto *pixel=latest.data()+i*pixel_bytes;
      if(pixel_bytes==1)pixel[0]=uint8_t(((background>>16)&224)|((background>>11)&28)|((background>>6)&3));
      else if(pixel_bytes==4)std::memcpy(pixel,&background,4);
      else {pixel[0]=background>>16;pixel[1]=background>>8;pixel[2]=background;}
    }
    fd = memfd_create("boayo-surface", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    require(fd >= 0, "memfd_create failed");
    if (ftruncate(fd, slot_bytes*2) ||
        fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL)) {
      close(fd);
      throw std::runtime_error("shared surface allocation failed");
    }
    void *p = mmap(nullptr, slot_bytes*2, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
      close(fd);
      throw std::runtime_error("shared surface mapping failed");
    }
    mapping = static_cast<uint8_t *>(p);
  }
  ~ApplicationBuffer() {
    if (mapping) munmap(mapping, slot_bytes*2);
    if (fd >= 0) close(fd);
  }
  ApplicationBuffer(const ApplicationBuffer &) = delete;
  ApplicationBuffer &operator=(const ApplicationBuffer &) = delete;

  J description() const {
    return {{"path", "/proc/"+std::to_string(getpid())+"/fd/"+std::to_string(fd)},
            {"format", format}, {"width", width}, {"height", height},
            {"stride", width*pixel_bytes}, {"slots", 2}, {"slot_bytes", slot_bytes},
            {"sequence", sequence}};
  }
  std::vector<Patch> snapshot(const J &args) const {
    require(args.at("sequence").is_number_unsigned() ||
            args.at("sequence").is_number_integer(), "invalid surface sequence");
    auto next = args.at("sequence").get<int64_t>();
    require(next > 0 && uint64_t(next) > sequence, "stale surface sequence");
    require(args.at("slot").is_number_integer(), "invalid shared slot");
    int64_t slot = args.at("slot").get<int64_t>();
    require(slot >= 0 && slot < 2, "invalid shared slot");
    auto &damage = args.at("damage");
    require(damage.is_array() && !damage.empty() && damage.size() <= 128,
            "damage must contain 1..128 rectangles");
    std::vector<Rect> areas;
    size_t total = 0;
    for (auto &item : damage) {
      require(item.is_array() && item.size() == 4, "invalid damage rectangle");
      for (auto &value : item)
        require(value.is_number_integer(), "damage coordinates must be integers");
      int64_t x=item[0], y=item[1], w=item[2], h=item[3];
      require(x >= 0 && y >= 0 && w > 0 && h > 0 &&
              x <= width && y <= height && w <= width-x && h <= height-y,
              "shared surface damage out of bounds");
      Rect r{int(x),int(y),int(w),int(h)};
      total += size_t(r.w)*r.h*pixel_bytes;
      require(total <= slot_bytes*2, "excessive overlapping damage");
      areas.push_back(r);
    }
    // Validate every rectangle and allocate snapshots before modifying a window.
    std::vector<Patch> patches;
    for (auto r : areas) patches.push_back({r, Bytes(size_t(r.w)*r.h*pixel_bytes)});
    const auto *source = mapping + size_t(slot)*slot_bytes;
    for (auto &patch : patches) {
      auto r = patch.area;
      for (int y = 0; y < r.h; ++y)
        std::memcpy(patch.rgb.data()+size_t(y)*r.w*pixel_bytes,
                    source+(size_t(r.y+y)*width+r.x)*pixel_bytes, size_t(r.w)*pixel_bytes);
    }
    return patches;
  }
};
}
