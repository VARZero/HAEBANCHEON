#include "backend.hpp"
#include "mask.hpp"
#include "culling.hpp"
#include <chrono>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include "init_trace.hpp"
#include "frame_trace.hpp"
namespace boayo {
static_assert(sizeof(bo_rgb_fill)==24, "BOSIO RGB DMA ABI layout");
bool LinuxDevice::rgb_fill(uint8_t *p,size_t n,const std::array<uint8_t,3> &color) {
  if(!(dma_features & BO_DMA_RGB_USER))return false;
  require(n<=BO_RGB_MAX_BYTES && !(n&3) && !(uintptr_t(p)&3),"RGB DMA bounds");
  bo_rgb_fill f{};f.data=uintptr_t(p);f.bytes=n;
  f.color=uint32_t(color[0])|(uint32_t(color[1])<<8)|(uint32_t(color[2])<<16);
  init_trace::mark("rgb.dma.begin",n);
  if(ioctl(fd,BO_RGB_FILL,&f)<0) {
    int error=errno;
    if(error==ETIMEDOUT) {
      // The kernel retains these pinned pages for a possible late write.
      // Exit the whole process without freeing/reusing the user allocation.
      static const char message[]="BOAYO_RGB_DMA_TIMEOUT_RESET_REQUIRED\n";
      const auto written=::write(STDERR_FILENO,message,sizeof(message)-1);
      (void)written;
      _exit(74);
    }
    throw std::runtime_error("BOSIO RGB DMA: "+std::string(strerror(error)));
  }
  init_trace::mark("rgb.dma.end",n);
  return true;
}
bool MockDevice::rgb_fill(uint8_t *p,size_t n,const std::array<uint8_t,3> &color) {
  if(!dma_enabled)return false;
  check();
  for(size_t i=0;i<n;i++)p[i]=color[i%3];
  ++dma_fills;
  return true;
}
static_assert(sizeof(bo_info) == 36 && sizeof(bo_upload) == 24 && sizeof(bo_run) == 40 &&
                  sizeof(bo_present) == 736 && sizeof(bo_pose) == 724,
              "BOSIO ABI layout");
static_assert(BO_SET_POSE == 0x42d44246UL, "BOSIO pose ioctl");
static_assert(sizeof(bo_mem_copy)==24 && sizeof(bo_mem_fill)==20 && sizeof(bo_dma_info)==72,
              "BOSIO optional DMA ABI layout");
static void call(int fd, unsigned long op, void *p = nullptr) {
  if (ioctl(fd, op, p) < 0)
    throw std::runtime_error("BOSIO ioctl " + std::to_string(op) + ": " + strerror(errno));
}
LinuxDevice::LinuxDevice(const std::string &path) {
  fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
  require(fd >= 0, "open " + path + ": " + strerror(errno));
  try {
    auto i = info();
    require(i.abi == 3 && !i.poisoned, "BOSIO ABI3 required, unpoisoned");
    bo_dma_info dma{};
    if(ioctl(fd,BO_GET_DMA_INFO,&dma)==0) {
      require(dma.version==1 && !dma.quarantined,"unsupported/quarantined BOSIO DMA");
      dma_features=dma.features;
      // Scratch belongs to this exclusive client. Display metadata stays unchanged.
      // Scratch is written by projection jobs before use; no blanket fill.
    } else require(errno==ENOTTY,"BOSIO DMA query: "+std::string(strerror(errno)));
  } catch (...) {
    close(fd);
    fd = -1;
    throw;
  }
}
LinuxDevice::~LinuxDevice() {
  if (fd >= 0) {
    try {
      stop();
    } catch (...) {
    }
    close(fd);
  }
}
bo_info LinuxDevice::info() {
  bo_info v{};
  call(fd, BO_GET_INFO, &v);
  return v;
}
void LinuxDevice::upload(unsigned r, unsigned off, const uint32_t *d, size_t n) {
  require(n <= UINT32_MAX / 4 && off <= UINT32_MAX / 4, "upload bounds");
  bo_upload v{};
  v.region = r;
  v.offset = off * 4;
  v.bytes = n * 4;
  v.data = uintptr_t(d);
  call(fd, BO_UPLOAD, &v);
}
void LinuxDevice::upload_direct(unsigned r,unsigned off,const uint32_t *p,size_t n){
  require(dma_features&BO_DMA_DIRECT_USER,"BOSIO direct user DMA required");
  require(n<=UINT32_MAX/4 && off<=UINT32_MAX/4,"direct upload bounds");
  bo_upload u{};u.region=r;u.offset=off*4;u.bytes=n*4;u.data=uintptr_t(p);
  if(ioctl(fd,BO_UPLOAD_DIRECT,&u)<0){int error=errno;
    if(error==ETIMEDOUT){static const char msg[]="BOAYO_DIRECT_DMA_TIMEOUT_RESET_REQUIRED\n";
      (void)!write(STDERR_FILENO,msg,sizeof(msg)-1);_exit(74);}
    throw std::runtime_error("BOSIO direct DMA: "+std::string(strerror(error)));}
}
void MockDevice::upload_direct(unsigned r,unsigned off,const uint32_t *p,size_t n){
  upload(r,off,p,n);trace.push_back({{"op","direct_upload"},{"bytes",n*4}});
}
Words LinuxDevice::download(unsigned off, unsigned n) {
  Words out(n);
  bo_upload v{};
  v.region = BO_SCRATCH;
  v.offset = off * 4;
  v.bytes = n * 4;
  v.data = uintptr_t(out.data());
  call(fd, BO_DOWNLOAD, &v);
  return out;
}
bo_run LinuxDevice::run(unsigned n, unsigned t, std::array<unsigned, 2> jobs,unsigned bank) {
  bo_run r{};
  r.bank = bank;
  r.scene_words = n;
  r.texture_words = t;
  r.jobs[0] = jobs[0];
  r.jobs[1] = jobs[1];
  r.timeout_ms = 30000;
  call(fd, BO_RUN, &r);
  return r;
}
void LinuxDevice::present(unsigned bank, unsigned n, unsigned resolution,
                          const std::array<int32_t, 180> &camera) {
  bo_present p{};
  p.bank = bank;
  p.scene_words = n;
  p.resolution = resolution;
  p.timeout_ms = 30000;
  std::copy(camera.begin(), camera.end(), p.camera);
  call(fd, BO_PRESENT, &p);
}
void LinuxDevice::pose(const std::array<int32_t, 180> &camera) {
  bo_pose p{};
  p.timeout_ms = 30000;
  std::copy(camera.begin(), camera.end(), p.camera);
  call(fd, BO_SET_POSE, &p);
}
void LinuxDevice::stop() {
  call(fd, BO_STOP);
}
bool LinuxDevice::copy(unsigned src,unsigned src_off,unsigned dst,unsigned dst_off,size_t n) {
  if(!(dma_features & BO_DMA_COPY)) return false;
  require(n<=UINT32_MAX/4 && src_off<=UINT32_MAX/4 && dst_off<=UINT32_MAX/4,"DMA copy bounds");
  bo_mem_copy c{src,src_off*4,dst,dst_off*4,uint32_t(n*4),0};
  call(fd,BO_MEM_COPY,&c);return true;
}
bool LinuxDevice::fill(unsigned region,unsigned off,size_t n,unsigned value) {
  if(!(dma_features & BO_DMA_FILL)) return false;
  require(n<=UINT32_MAX/4 && off<=UINT32_MAX/4 && value<=255,"DMA fill bounds");
  bo_mem_fill f{region,off*4,uint32_t(n*4),value,0};
  call(fd,BO_MEM_FILL,&f);return true;
}
J LinuxDevice::dma_status() {
  if(!dma_features)return {{"available",false}};
  bo_dma_info d{};call(fd,BO_GET_DMA_INFO,&d);
  return {{"available",true},{"features",d.features},{"upload_threshold",d.upload_threshold},
          {"copy_ops",d.copy_ops},{"copy_bytes",d.copy_bytes},{"fill_ops",d.fill_ops},
          {"fill_bytes",d.fill_bytes},{"upload_ops",d.upload_ops},{"upload_bytes",d.upload_bytes},
          {"errors",d.errors},{"timeouts",d.timeouts},{"quarantined",d.quarantined}};
}
MockDevice::MockDevice() {
  regions[0].resize(0x100000);
  regions[1].resize(64 * 18);
  regions[2].resize(64 * 18);
  regions[3].resize(BO_MAX_WORDS);
  regions[4].resize(BO_MAX_WORDS);
  regions[5].resize(0x10000);
}
void MockDevice::check() {
  require(!poisoned, "mock device poisoned");
  if (fail_after == 0) {
    poisoned = true;
    throw std::runtime_error("injected backend failure");
  }
  if (fail_after > 0)
    --fail_after;
}
bo_info MockDevice::info() {
  return {3,
          0,
          0,
          unsigned((active < 0 ? 0 : 3) | (frames << 16)),
          0,
          active,
          unsigned(poisoned),
          64,
          BO_MAX_WORDS};
}
void MockDevice::upload(unsigned r, unsigned off, const uint32_t *d, size_t n) {
  check();
  require(r < 6 && uint64_t(off) + n <= regions[r].size(), "mock upload bounds");
  require(!(r == 3 + unsigned(active) && active >= 0), "active scanout bank write");
  std::copy_n(d, n, regions[r].begin() + off);
  trace.push_back({{"op", "upload"}, {"region", r}, {"offset", off}, {"words", n}});
}
Words MockDevice::download(unsigned off, unsigned n) {
  check();
  require(uint64_t(off) + n <= regions[5].size(), "mock download bounds");
  trace.push_back({{"op", "download"}, {"offset", off}, {"words", n}});
  return Words(regions[5].begin() + off, regions[5].begin() + off + n);
}
bo_run MockDevice::run(unsigned n, unsigned t, std::array<unsigned, 2> jobs,unsigned bank) {
  check();
  bo_run r{};
  require(bank<=2 && (bank==2 || int(bank)!=active) && n <= regions[3+bank].size() && t <= regions[0].size(), "mock run bounds");
  r.bank = bank;
  r.scene_words = n;
  r.texture_words = t;
  Words tex(regions[0].begin(), regions[0].begin() + t);
  for (unsigned core = 0; core < 2; core++) {
    require(jobs[core] <= 64, "mock queue overflow");
    r.jobs[core] = jobs[core];
    for (unsigned j = 0; j < jobs[core]; j++) {
      Descriptor d;
      std::copy_n(regions[1 + core].begin() + j * 18, 18, d.begin());
      require(uint64_t(d[1]) + 256 <= n, "mock scratch bounds");
      r.cells[core] += project_reference(tex, regions[3+bank], d);
    }
  }
  trace.push_back({{"op", "run"}, {"bank",bank}, {"scene_words", n}, {"texture_words", t}, {"jobs", jobs}});
  return r;
}
void MockDevice::present(unsigned bank, unsigned n, unsigned res,
                         const std::array<int32_t, 180> &camera) {
  check();
  require(bank < 2 && int(bank) != active && n <= BO_MAX_WORDS && res == 2, "mock present bounds");
  const auto &scene=regions[3+bank];
  unsigned command=active>=0 && active_words==n && active_metadata.size()==header &&
    std::equal(active_metadata.begin(),active_metadata.end(),scene.begin())?2:1;
  active_metadata.assign(scene.begin(),scene.begin()+header);active_words=n;
  active = bank;
  ++frames;
  trace.push_back({{"command",command},{"op", "present"}, {"bank", bank}, {"words", n}, {"camera", camera}});
}
void MockDevice::pose(const std::array<int32_t, 180> &camera) {
  check();
  require(active >= 0, "pose before presentation");
  ++frames;
  trace.push_back({{"op", "pose"}, {"camera", camera}});
}
void MockDevice::stop() {
  active = -1;
  trace.push_back({{"op", "stop"}});
}
bool MockDevice::copy(unsigned src,unsigned src_off,unsigned dst,unsigned dst_off,size_t n) {
  if(!dma_enabled)return false;
  check();
  require(src<6 && dst<6 && n && uint64_t(src_off)+n<=regions[src].size() &&
          uint64_t(dst_off)+n<=regions[dst].size(),"mock DMA copy bounds");
  require(!(active>=0 && dst==3+unsigned(active)),"active scanout DMA write");
  require(src!=dst || uint64_t(src_off)+n<=dst_off || uint64_t(dst_off)+n<=src_off,
          "overlapping DMA copy");
  std::copy_n(regions[src].begin()+src_off,n,regions[dst].begin()+dst_off);
  ++dma_copies;
  trace.push_back({{"op","copy"},{"src",src},{"src_offset",src_off},
                   {"dst",dst},{"dst_offset",dst_off},{"words",n}});
  return true;
}
bool MockDevice::fill(unsigned region,unsigned off,size_t n,unsigned value) {
  if(!dma_enabled)return false;
  check();
  require(region<6 && n && value<=255 && uint64_t(off)+n<=regions[region].size(),"mock DMA fill bounds");
  require(!(active>=0 && region==3+unsigned(active)),"active scanout DMA fill");
  std::fill_n(regions[region].begin()+off,n,uint32_t(value)*0x01010101u);
  ++dma_fills;trace.push_back({{"op","fill"},{"region",region},{"offset",off},{"words",n},{"value",value}});
  return true;
}
J MockDevice::dma_status() {
  return {{"available",dma_enabled},{"copy_ops",dma_copies},{"fill_ops",dma_fills}};
}
void Output::commit(size_t scene_size, const std::vector<Range> &ranges) {
  require(!failed, "previous upload failed; restart required");
  unsigned target = active < 0 ? 0 : unsigned(active) ^ 1;
  try {
    for (auto &r : ranges) {
      dev.upload(3 + target, r.offset, r.data, r.count);
      uploaded_words += r.count;
    }
    dev.present(target, scene_size, 2, camera);
    active = target;
    ++commits;
    for (auto &r : ranges) {
      if(dev.copy(3+target,r.offset,3+(target^1),r.offset,r.count)) dma_copied_words+=r.count;
      else {dev.upload(3 + (target ^ 1), r.offset, r.data, r.count);uploaded_words += r.count;}
    }
  } catch (...) {
    failed = true;
    throw;
  }
}
void Output::full(const Words &s, bool fixed_active_only) {
  require(s.size() >= 4480 && s.size() <= BO_MAX_WORDS && s.size() % 16 == 0,
          "invalid full scene length");
  std::array<uint32_t, 256> palette;
  for (unsigned k = 0; k < 256; k++) {
    auto v = s[k];
    palette[k] = (v & 0xff0000) | ((v & 255) << 8) | ((v >> 8) & 255);
  }
  std::vector<Range> ranges{{0, palette.data(), palette.size()}};
  if (fixed_active_only) {
    require(s.size() == scene_words, "fixed initialization size");
    ranges.push_back({256, s.data() + 256, header - 256});
    for (unsigned tile = 0; tile < tiles;) {
      if (s[256 + tile] == 0xffffffff) {
        ++tile;
        continue;
      }
      unsigned first = tile;
      do {
        require(s[256 + tile] == tile * 1024, "fixed initialization directory");
        ++tile;
      } while (tile < tiles && s[256 + tile] != 0xffffffff);
      ranges.push_back(
          {header + first * 256, s.data() + header + first * 256, (tile - first) * 256});
    }
  } else
    ranges.push_back({256, s.data() + 256, s.size() - 256});
  // Invalid directory entries make skipped slots unreachable. Every BPT2 activation
  // uploads its complete tile before presentation; stale RAM is never made visible.
  // Allocate before touching the device. Palette conversion never copies the tile body.
  shadow.resize(s.size());
  commit(s.size(), ranges);
  std::copy(palette.begin(), palette.end(), shadow.begin());
  std::copy(s.begin() + 256, s.end(), shadow.begin() + 256);
  shadow_copied_words += s.size();
  blank_only = false;
}
void Output::initialize_blank() {
  // Hardware interprets every invalid directory entry as black. No tile bodies.
  Words metadata((header + 15) & ~15u, 0);
  for (unsigned k = 0; k < 256; ++k)
    metadata[k] = palette(k);
  std::fill(metadata.begin() + 256, metadata.begin() + header, 0xffffffff);
  // The hardware reads 4480 words but cell data begins at word 4476.
  // Never write the alignment tail into the first tile, even for blank output.
  for(unsigned k=0;k<256;++k){auto v=metadata[k];metadata[k]=(v&0xff0000)|((v&255)<<8)|((v>>8)&255);}
  require(!failed,"previous upload failed; restart required");
  unsigned target=active<0?0:unsigned(active)^1;
  try {
    uploaded_words+=dev.publish(target,metadata.size(),2,camera,metadata);active=target;++commits;
    // No eager mirror: the resident scheduler prepares the other bank on demand.
    shadow=std::move(metadata);blank_only=true;
  }catch(...){failed=true;throw;}
}
void Output::patch(const Words &p) {
  if (p.empty())
    return;
  require(!blank_only, "metadata-only blank requires a complete scene before patches");
  require(!shadow.empty() && p.size() >= 16 && (p[0] == 0x42505431 || p[0] == 0x42505432),
          "BPT1/BPT2 requires a full scene");
  bool bpt2 = p[0] == 0x42505432;
  unsigned size = p[1], count = p[2];
  require(size == 256 && count <= tiles && p[3] == p.size() &&
              p.size() == 16 + uint64_t(count) * (16 + size),
          "invalid patch size");
  std::vector<Range> ranges;
  ranges.reserve(count * 2);
  for (unsigned n = 0; n < count; n++) {
    unsigned start = 16 + n * (16 + size), off = p[start], tile = p[start + 1];
    require(tile < tiles && uint64_t(off) + header + size <= shadow.size(),
            "patch directory bounds");
    if (bpt2) {
      auto e = p[start + 2];
      require(off == tile * 256 && (e == off * 4 || e == 0xffffffff), "BPT2 fixed slot mismatch");
      ranges.push_back({256 + tile, p.data() + start + 2, 1});
    } else
      require(shadow[256 + tile] == off * 4, "BPT1 directory mismatch");
    ranges.push_back({header + off, p.data() + start + 16, size});
  }
  if (ranges.empty())
    return;
  commit(shadow.size(), ranges);
  for (auto &r : ranges) {
    std::copy_n(r.data, r.count, shadow.begin() + r.offset);
    shadow_copied_words += r.count;
  }
}
void Output::set_pose(double yaw, double pitch, double roll) {
  auto c = geometry.camera(yaw, pitch, roll);
  try {
    dev.pose(c);
    camera = c;
  } catch (...) {
    failed = true;
    throw;
  }
}
J Output::status() {
  auto i = dev.info();
  return {{"abi", i.abi},
          {"signature", i.signature},
          {"capability", i.capability},
          {"status", i.status},
          {"active_base", i.active_base},
          {"active_bank", i.active_bank},
          {"poisoned", i.poisoned},
          {"max_jobs", i.max_jobs},
          {"max_scene_words", i.max_scene_words},
          {"backend", "kfb-linux-frame-atomic-output"},
          {"commits", commits},
          {"publish_profile",dev.publish_stats()},{"uploaded_words", uploaded_words},
          {"shadow_copied_words", shadow_copied_words},
          {"dma_copied_words", dma_copied_words},
          {"memory_dma", dev.dma_status()},
          {"metadata_only", blank_only},
          {"sensor_available", false},
          {"antialias", false}};
}
bo_run Projectors::run(WordView texture, const std::vector<Descriptor> &desc, Words &scene, std::shared_ptr<const Texture> immutable) {
  require(!failed, "previous projection failed; restart required");
  require(!texture.empty() && texture.size() <= 0x100000, "invalid texture allocation");
  std::vector<unsigned> dest;
  for (auto &d : desc) {
    require(uint64_t(d[1]) + 256 <= scene.size() && d[2] >= 1 && d[2] <= 2048 && d[3] >= 1 &&
                d[3] <= 2048 && d[4] >= d[2] &&
                uint64_t(d[0]) + uint64_t(d[3] - 1) * d[4] + d[2] <= texture.size() &&
                !(d[5] & ~0xff01u),
            "invalid projection descriptor");
    dest.push_back(d[1]);
  }
  std::sort(dest.begin(), dest.end());
  for (size_t i = 1; i < dest.size(); i++)
    require(dest[i - 1] + 256 <= dest[i], "overlapping projection destinations");
  bo_run total{};
  if (desc.empty())
    return total;
  try {
    require(!immutable || immutable->data()==texture.data() && immutable->size()==texture.size(),"immutable texture identity");
    if(!immutable || resident_texture!=immutable) {
      resident_texture.reset();
      frame_trace::mark("texture.upload.begin",texture.size());
      if(immutable)dev.upload_texture(*immutable);
      else dev.upload(0, 0, texture.data(), texture.size());
      resident_texture=immutable;++texture_uploads;
      frame_trace::mark("texture.upload.end",texture.size());
    } else ++texture_reuses;
    frame_trace::mark("projectors.begin",desc.size());
    for (size_t first = 0; first < desc.size(); first += 128) {
      unsigned n = std::min(size_t(128), desc.size() - first);
      std::array<Words, 2> queues;
      for (unsigned i = 0; i < n; i++) {
        auto d = desc[first + i];
        if (!(d[5] & 1))
          dev.upload(5, header + i * 256, scene.data() + d[1], 256);
        d[1] = header + i * 256;
        queues[i % 2].insert(queues[i % 2].end(), d.begin(), d.end());
      }
      std::array<unsigned, 2> jobs;
      for (unsigned core = 0; core < 2; core++) {
        jobs[core] = queues[core].size() / 18;
        if (jobs[core])
          dev.upload(1 + core, 0, queues[core].data(), queues[core].size());
      }
      auto result = dev.run((header + n * 256 + 15) & ~15u, texture.size(), jobs);
      require(!result.status[0] && !result.status[1], "projection core error");
      auto words = dev.download(header, n * 256);
      for (unsigned i = 0; i < n; i++)
        std::copy_n(words.begin() + i * 256, 256, scene.begin() + desc[first + i][1]);
      for (unsigned core = 0; core < 2; core++) {
        total.jobs[core] += jobs[core];
        total.cells[core] += result.cells[core];
      }
    }
  } catch (...) {
    resident_texture.reset();
    failed = true;
    throw;
  }
  frame_trace::mark("projectors.end",desc.size());
  return total;
}
static int64_t upper(const std::array<int64_t, 4> &p) {
  return std::max({p[0], p[0] + 31 * p[1], p[0] + 31 * (p[1] + p[2])}) + std::max(int64_t(0), p[3]);
}
static bool affected(const Descriptor &d, int x, int y, int w, int h) {
  std::array<std::array<int64_t, 4>, 5> p;
  for (int k = 0; k < 4; k++) {
    int64_t u = int32_t(d[6 + k]), v = int32_t(d[10 + k]), z = int32_t(d[14 + k]);
    p[0][k] = z;
    p[1][k] = u - (x - 1) * z;
    p[2][k] = (x + w) * z - u;
    p[3][k] = v - (y - 1) * z;
    p[4][k] = (y + h) * z - v;
  }
  for (int j = 0; j < 5; j++)
    if (j == 0 ? upper(p[j]) <= 0 : upper(p[j]) < 0)
      return false;
  return true;
}
Compositor::Compositor(const Geometry &g, Projectors &p, bool defer_scene,bool resident)
    : geometry(g), projectors(p),resident_only(resident) {
  if (defer_scene) {
    // Known startup state: RGB332 background index 0, no windows or pointer.
    // The first real render dirties only actual window/pointer footprints.
    previous_bg = rgb_index(2, 6, 14);
    pointer_key = J::array({false});
  } else {
    allocate_scene();
  }
  metrics = {{"hardware_frames", 0}, {"fallback_frames", fallback_frames}};
}
void Compositor::allocate_scene() {
  if (!scene.empty()) return;
  frame_trace::mark("scene.allocate.begin",scene_words);
  scene.assign(scene_words, 0);
  for (unsigned k = 0; k < 256; k++)
    scene[k] = palette(k);
  std::fill(scene.begin() + 256, scene.begin() + header, 0xffffffff);
  frame_trace::mark("scene.allocate.end",scene_words);
}
static J geometry_key(const Window &w) {
  auto &d = w.data;
  return J::array({d["surface_width"], d["surface_height"], d["azimuth"], d["elevation"], d["roll"],
                   d["width_deg"], d["height_deg"]});
}
Layer Compositor::make_layer(const Window &w) {
  Layer c;
  c.key = geometry_key(w);
  frame_trace::mark("plan.load.begin");
  c.plan=load_projection_plan(c.key,w.data["shape"]);
  frame_trace::mark("plan.load.end",bool(c.plan));
  if(c.plan){
    c.ids=c.plan->ids;c.desc=c.plan->descriptors;c.mask_kinds=c.plan->kinds;c.shape=w.data["shape"];
    for(unsigned i=0;i<c.ids.size();++i){c.lookup[c.ids[i]]=i;c.invalid.insert(i);}
    if(!resident_only)c.pixels.resize(c.ids.size()*256);++precomputed_layers;return c;
  }
  frame_trace::mark("geometry.compute.begin");geometry_tiles_evaluated+=tiles;
  auto &v = w.data;
  Geometry::Projection projection(v["surface_width"], v["surface_height"], v["azimuth"],
                                  v["elevation"], v["roll"], v["width_deg"], v["height_deg"]);
  auto groups=candidate_groups(projection);
  for (unsigned t = 0; t < tiles; t++) {
    if(!groups[t/32])continue;
    auto [d, visible] = geometry.descriptor(t, projection);
    if (!visible)
      continue;
    c.lookup[t] = c.ids.size();
    c.invalid.insert(c.ids.size());
    c.ids.push_back(t);
    c.desc.push_back(d);
  }
  if(!resident_only)c.pixels.resize(c.ids.size() * 256);
  frame_trace::mark("geometry.compute.end",c.ids.size());
  return c;
}
void Compositor::prepare_masks(Layer &c,const Window &w){
  c.shape=w.data["shape"];
  auto plan=load_projection_plan(c.key,c.shape);
  if(plan && plan->ids==c.ids && plan->descriptors==c.desc){
    c.plan=plan;c.mask_kinds=plan->kinds;c.masks.clear();++precomputed_layers;return;
  }
  c.plan.reset();frame_trace::mark("mask.compute.begin",c.ids.size());
  c.masks.assign(c.ids.size(),Bytes(1024));c.packed_masks.assign(c.ids.size(),{});c.mask_kinds.assign(c.ids.size(),2);
  auto started=std::chrono::steady_clock::now();MaskShape shape(c.shape);
  const char *reference=getenv("BOAYO_MASK_REFERENCE");
  if(!reference || std::string(reference)!="1") {
    for(unsigned i=0;i<c.ids.size();++i){bool whole=false;
      c.mask_kinds[i]=prepare_mask(c.desc[i],shape,c.masks[i],c.packed_masks[i],whole);
      mask_fast_tiles+=whole;mask_samples_evaluated+=whole?0:1024;
    }
    mask_precise_samples+=shape.precise_samples;mask_integer_samples+=shape.integer_samples;
  } else {
  mask_samples_evaluated+=c.ids.size()*1024;
  for(unsigned i=0;i<c.ids.size();++i){bool any=false,all=true;
    for(unsigned cell=0;cell<1024;++cell){auto q=coordinates(c.desc[i],cell);
      bool v=covered(c.desc[i],q) && (c.shape.empty() || shape_contains(c.shape,q[2]>0?double(q[0])/q[2]:0,q[2]>0?double(q[1])/q[2]:0));
      c.masks[i][cell]=v;if(v)c.packed_masks[i][cell/32]|=1u<<(cell%32);any|=v;all&=v;
    }
    c.mask_kinds[i]=!any?0:all?1:2;
  }
  }
  ++mask_builds;mask_build_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
  frame_trace::mark("mask.compute.end",c.ids.size());
}
J Compositor::export_plan(const Window &w,const std::string &path){
  // Build tool sets BOAYO_PLANS=0; export uses the uncached algorithm.
  auto c=make_layer(w);prepare_masks(c,w);require(!c.plan,"export requires BOAYO_PLANS=0");
  Words records(c.ids.size()*21);Bytes masks;
  unsigned full=0,empty=0,boundary=0;
  for(unsigned i=0;i<c.ids.size();++i){auto *r=records.data()+i*21;r[0]=c.ids[i];r[1]=c.mask_kinds[i];
    if(r[1]==2){r[2]=records.size()*4+masks.size();masks.insert(masks.end(),c.masks[i].begin(),c.masks[i].end());
      for(uint32_t bits:c.packed_masks[i])for(unsigned byte=0;byte<4;++byte)masks.push_back(uint8_t(bits>>(byte*8)));++boundary;}
    else if(r[1]==1)++full;else ++empty;
    std::copy(c.desc[i].begin(),c.desc[i].end(),r+3);
  }
  Bytes body(records.size()*4+masks.size());std::memcpy(body.data(),records.data(),records.size()*4);
  std::copy(masks.begin(),masks.end(),body.begin()+records.size()*4);write_bytes(path,body.data(),body.size());
  return {{"key",projection_plan_key(c.key,w.data["shape"])},{"count",c.ids.size()},{"bytes",body.size()},
          {"full",full},{"empty",empty},{"boundary",boundary},{"geometry",c.key},{"shape",w.data["shape"]}};
}
void Compositor::source_changes(Layer &c, const Window &w) {
  if (c.revision == w.revision)
    return;
  if(w.prepared) {
    require(w.prepared->size()==size_t(w.data["surface_width"].get<int>())*w.data["surface_height"].get<int>(),"prepared texture size");
    if(c.prepared_source!=w.prepared) {
      auto r=w.dirty;
      for(unsigned i=0;i<c.ids.size();++i)
        if(!c.prepared_source || !r[2] || affected(c.desc[i],r[0],r[1],r[2],r[3]))c.invalid.insert(i);
    }
    c.prepared_source=w.prepared;c.revision=w.revision;
    return;
  }
  w.ensure_surface();
  if(c.prepared_source) {c.prepared_source.reset();c.source.clear();}
  int width = w.data["surface_width"], height = w.data["surface_height"];
  bool fresh = c.source.size() != size_t(width) * height;
  if (fresh) {
    c.source.resize(size_t(width) * height);
    for (unsigned i = 0; i < c.ids.size(); i++)
      c.invalid.insert(i);
  }
  auto r = w.dirty;
  if (fresh || !r[2])
    r = {0, 0, width, height};
  int minx = width, miny = height, maxx = -1, maxy = -1;
  for (int y = r[1]; y < r[1] + r[3]; y++)
    for (int x = r[0]; x < r[0] + r[2]; x++) {
      size_t p = size_t(y) * width + x;
      uint32_t value = (uint32_t(w.surface[p * 3]) << 16) | (uint32_t(w.surface[p * 3 + 1]) << 8) |
                       w.surface[p * 3 + 2];
      if (c.source[p] != value) {
        c.source[p] = value;
        minx = std::min(minx, x);
        miny = std::min(miny, y);
        maxx = std::max(maxx, x);
        maxy = std::max(maxy, y);
      }
    }
  if (!fresh && maxx >= 0)
    for (unsigned i = 0; i < c.ids.size(); i++)
      if (affected(c.desc[i], minx, miny, maxx - minx + 1, maxy - miny + 1))
        c.invalid.insert(i);
  c.revision = w.revision;
}
Composition Compositor::update(WM &wm, bool full) {
  allocate_scene();
  last_hardware = false;
  for (auto &[id, w] : wm.windows)
    if (w.data["mapped"].get<bool>() &&
        (w.data["decorated"].get<bool>() || w.data["width_deg"].get<double>() >= 160 ||
         w.data["surface_width"].get<int>() > 2048 || w.data["surface_height"].get<int>() > 2048)) {
      if (!fallback)
        fallback = std::make_unique<NativeRenderer>(geometry);
      Composition r;
      r.words = fallback->packed(wm);
      valid = false;
      ++fallback_frames;
      metrics = {{"mode", "cpp-scalar-fallback"},
                 {"reason", "decoration or dimensions require native renderer"},
                 {"jobs_per_core", {0, 0}},
                 {"geometry_tiles_evaluated_total",geometry_tiles_evaluated},
             {"mask_samples_evaluated_total",mask_samples_evaluated},
             {"precomputed_layers_total",precomputed_layers},
             {"plan_cache",projection_plan_stats()},
             {"hardware_frames", frames},
                 {"fallback_frames", fallback_frames}};
      return r;
    }
  last_hardware = true;
  std::vector<int> order;
  for (int id : wm.order)
    if (wm.windows[id].data["mapped"].get<bool>())
      order.push_back(id);
  std::set<unsigned> dirty;
  for (int id : previous_order)
    if (std::find(order.begin(), order.end(), id) == order.end())
      dirty.insert(previous_tiles[id].begin(), previous_tiles[id].end());
  for (auto i = layers.begin(); i != layers.end();)
    if (!wm.windows.count(i->first))
      i = layers.erase(i);
    else
      ++i;
  std::array<unsigned, 2> jobs{}, cells{};
  unsigned uploaded = 0, reused = 0;
  std::vector<int> projected;
  std::map<int, std::set<unsigned>> footprints;
  for (int id : order) {
    auto &w = wm.windows[id];
    auto found = layers.find(id);
    if (found == layers.end() || found->second.key != geometry_key(w)) {
      dirty.insert(previous_tiles[id].begin(), previous_tiles[id].end());
      auto c = make_layer(w);
      if(found!=layers.end() && found->second.prepared_source) {
        c.prepared_source=found->second.prepared_source;c.revision=found->second.revision;
      }
      if (found != layers.end() && found->second.source.size() == w.surface.size() / 3) {
        c.source = std::move(found->second.source);
        c.revision = found->second.revision;
      }
      layers[id] = std::move(c);
      dirty.insert(layers[id].ids.begin(), layers[id].ids.end());
    }
    auto &c = layers[id];
    if (c.shape != w.data["shape"]) {
      auto previous_mask_kinds=c.mask_kinds;
      prepare_masks(c,w);
      for(unsigned i=0;i<c.ids.size();++i)
        if(i>=previous_mask_kinds.size() || (!previous_mask_kinds[i] && c.mask_kinds[i]))c.invalid.insert(i);
      dirty.insert(c.ids.begin(), c.ids.end());
    }
    source_changes(c, w);
    if (std::find(previous_order.begin(), previous_order.end(), id) == previous_order.end())
      dirty.insert(c.ids.begin(), c.ids.end());
    reused += c.ids.size() - c.invalid.size();
    // Fully clipped tiles need no projection. Revealing them invalidates above.
    for(auto i=c.invalid.begin();i!=c.invalid.end();)if(!c.mask_kinds[*i])i=c.invalid.erase(i);else ++i;
    std::vector<Descriptor> todo;
    for (auto i : c.invalid) {
      dirty.insert(c.ids[i]);
      auto d = c.desc[i];
      d[1] = todo.size() * 256;
      todo.push_back(d);
    }
    if (!todo.empty()) {
      Words result(todo.size() * 256);
      WordView texture=c.prepared_source?WordView(*c.prepared_source):WordView(c.source);
      auto previous_uploads=projectors.texture_uploads;
      auto timing = projectors.run(texture, todo, result,c.prepared_source);
      unsigned j = 0;
      for (auto i : c.invalid) {
        std::copy_n(result.begin() + j * 256, 256, c.pixels.begin() + i * 256);
        j++;
      }
      c.invalid.clear();
      for (unsigned core = 0; core < 2; core++) {
        jobs[core] += timing.jobs[core];
        cells[core] += timing.cells[core];
        total_jobs += timing.jobs[core];
      }
      if(projectors.texture_uploads!=previous_uploads)uploaded += texture.size();
      projected.push_back(id);
    }
    footprints[id] = {c.ids.begin(), c.ids.end()};
  }
  std::map<int, int> oldpos;
  for (size_t i = 0; i < previous_order.size(); i++)
    oldpos[previous_order[i]] = i;
  for (size_t i = 0; i < order.size(); i++)
    for (size_t j = i + 1; j < order.size(); j++) {
      int a = order[i], b = order[j];
      if (oldpos.count(a) && oldpos.count(b) && oldpos[a] > oldpos[b])
        for (auto t : footprints[a])
          if (footprints[b].count(t))
            dirty.insert(t);
    }
  int bg = rgb_index(2, 6, 14);
  if (bg != previous_bg)
    for (unsigned t = 0; t < tiles; t++)
      dirty.insert(t);
  J pk = wm.pointer_visible ? J::array({wm.paz, wm.pel, true}) : J::array({false});
  std::set<unsigned> pointer_tiles;
  if (pk != pointer_key) {
    pointer_outer.clear();
    pointer_inner.clear();
    if (wm.pointer_visible) {
      V direction = axes(wm.paz, wm.pel)[0];
      for (auto &x : direction)
        x = float(x);
      for (unsigned t = 0; t < tiles; t++) {
        auto tri = geometry.triangles[t];
        V center = unit((tri[0] + tri[1] + tri[2]) * (1. / 3));
        double min_dot = 1;
        for (auto q : tri)
          min_dot = std::min(min_dot, dot(unit(q), center));
        double threshold = std::cos(std::acos(std::clamp(min_dot, -1., 1.)) + rad(2.2) + 1e-6);
        if (dot(center, direction) < threshold)
          continue;
        for (unsigned cell = 0; cell < 1024; cell++) {
          V q = geometry.ray(t, cell);
          for (auto &x : q)
            x = float(x);
          float d = float(float(float(q[0] * direction[0]) + float(q[1] * direction[1])) +
                          float(q[2] * direction[2]));
          if (d >= float(std::cos(rad(2.2))))
            pointer_outer.push_back(t * 1024 + cell);
          if (d >= float(std::cos(rad(1.))))
            pointer_inner.push_back(t * 1024 + cell);
        }
      }
    }
    for (auto i : pointer_outer)
      pointer_tiles.insert(i / 1024);
    for (auto i : pointer_inner)
      pointer_tiles.insert(i / 1024);
    dirty.insert(previous_pointer_tiles.begin(), previous_pointer_tiles.end());
    dirty.insert(pointer_tiles.begin(), pointer_tiles.end());
    pointer_key = pk;
  } else
    pointer_tiles = previous_pointer_tiles;
  frame_trace::mark("tile.compose.begin",dirty.size());
  std::vector<unsigned> changed;
  for (unsigned t : dirty) {
    std::array<uint8_t, 1024> data;
    data.fill(bg);
    for (int id : order) {
      auto &c = layers[id];
      int index = c.lookup[t];
      if (index < 0)
        continue;
      auto *p = reinterpret_cast<const uint8_t *>(c.pixels.data() + index * 256);
      unsigned kind=c.mask_kinds[index];
      if(kind==0)continue;
      if(kind==1){std::copy_n(p,1024,data.begin());continue;}
      const auto *mask=c.mask(index);
      for(unsigned cell=0;cell<1024;++cell)if(mask[cell])data[cell]=p[cell];
    }
    for (auto p : pointer_outer)
      if (p / 1024 == t)
        data[p % 1024] = rgb_index(10, 10, 10);
    for (auto p : pointer_inner)
      if (p / 1024 == t)
        data[p % 1024] = 255;
    auto *old = reinterpret_cast<uint8_t *>(scene.data() + header + t * 256);
    if (!std::equal(data.begin(), data.end(), old)) {
      std::copy(data.begin(), data.end(), old);
      scene[256 + t] = std::any_of(data.begin(), data.end(), [](uint8_t c) { return c != 0; })
                           ? t * 1024
                           : 0xffffffff;
      changed.push_back(t);
    }
  }
  frame_trace::mark("tile.compose.end",changed.size());
  previous_order = order;
  previous_tiles = std::move(footprints);
  previous_bg = bg;
  previous_pointer_tiles = pointer_tiles;
  for (int id : order)
    wm.windows[id].dirty = {};
  Composition result;
  result.changed = changed.size();
  result.patch = !full && valid;
  if (!result.patch)
    result.scene_view = &scene;
  else if (!changed.empty()) {
    result.words.assign(16 + changed.size() * 272, 0);
    auto &p = result.words;
    p[0] = 0x42505432;
    p[1] = 256;
    p[2] = changed.size();
    p[3] = p.size();
    unsigned pos = 16;
    for (unsigned t : changed) {
      p[pos] = t * 256;
      p[pos + 1] = t;
      p[pos + 2] = scene[256 + t];
      std::copy_n(scene.begin() + header + t * 256, 256, p.begin() + pos + 16);
      pos += 272;
    }
  }
  ++frames;
  metrics = {{"mode", "kfb-dual-projectors+cpu-tile-composition"},
             {"geometry_tiles_evaluated_total",geometry_tiles_evaluated},
             {"mask_samples_evaluated_total",mask_samples_evaluated},
             {"precomputed_layers_total",precomputed_layers},
             {"plan_cache",projection_plan_stats()},
             {"hardware_frames", frames},
             {"fallback_frames", fallback_frames},
             {"windows", order},
             {"projected_windows", projected},
             {"jobs_per_core", jobs},
             {"written_cells_per_core", cells},
             {"texture_upload_words", uploaded},
             {"texture_uploads_total", projectors.texture_uploads},
             {"texture_reuses_total", projectors.texture_reuses},
             {"reused_window_tiles", reused},
             {"dirty_tiles", dirty.size()},
             {"changed_tiles", changed.size()},
             {"total_hw_jobs", total_jobs},
             {"update_kind", result.patch ? "patch" : "full"},
             {"output_words", result.data().size()},
             {"active_tiles", std::count_if(scene.begin() + 256, scene.begin() + header,
                                            [](uint32_t v) { return v != 0xffffffff; })},
             {"sampling", "fixed-point bilinear RGB332"}};
  return result;
}
} // namespace boayo
