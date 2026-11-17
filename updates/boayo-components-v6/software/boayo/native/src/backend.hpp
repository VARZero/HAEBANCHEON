#pragma once
#include "geometry.hpp"
#include "wm.hpp"
#include "fallback.hpp"
#include "plan.hpp"
#include "../../../bosio/include/bosio_uapi.h"
#include <memory>
#include "texture.hpp"
namespace boayo {
struct Device {
  virtual ~Device() = default;
  virtual bo_info info() = 0;
  virtual void upload(unsigned region, unsigned offset, const uint32_t *data, size_t words) = 0;
  virtual void upload_direct(unsigned r,unsigned o,const uint32_t *p,size_t n){upload(r,o,p,n);}
  void upload_texture(const Texture &t){
    upload_direct(0,0,t.data(),t.size());
    for(const auto &p:t.patches){
      if(p.width==p.src_stride && p.width==p.dst_stride)
        upload_direct(0,p.dst,p.source->data()+p.src,size_t(p.width)*p.height);
      else for(unsigned y=0;y<p.height;++y)
        upload_direct(0,p.dst+y*p.dst_stride,p.source->data()+p.src+y*p.src_stride,p.width);
    }
  }
  virtual Words download(unsigned offset, unsigned words) = 0;
  virtual bo_run run(unsigned scene_count, unsigned texture_count,
                     std::array<unsigned, 2> jobs, unsigned bank=2) = 0;
  virtual void present(unsigned bank, unsigned count, unsigned resolution,
                       const std::array<int32_t, 180> &camera) = 0;
  virtual unsigned publish(unsigned,unsigned,unsigned,const std::array<int32_t,180> &,const Words &);
  virtual J publish_stats(){return J::object();}
  virtual void pose(const std::array<int32_t, 180> &camera) = 0;
  virtual void stop() = 0;
  virtual void batch(const std::vector<bo_batch_entry> &);
  virtual void batch_view(const bo_batch_entry *p,size_t n){batch(std::vector<bo_batch_entry>(p,p+n));}
  virtual void blend(const bo_tile_blend &) = 0;
  virtual bool copy(unsigned,unsigned,unsigned,unsigned,size_t) { return false; }
  virtual bool fill(unsigned,unsigned,size_t,unsigned) { return false; }
  virtual J dma_status() { return J::object(); }
  virtual bool rgb_fill(uint8_t *, size_t, const std::array<uint8_t,3> &) { return false; }
};
struct LinuxDevice : Device {
  int fd = -1;
  unsigned dma_features = 0;
  bo_publish_profile last_publish{};
  explicit LinuxDevice(const std::string &path = "/dev/bosio0");
  ~LinuxDevice();
  bo_info info() override;
  void upload(unsigned, unsigned, const uint32_t *, size_t) override;
  void upload_direct(unsigned,unsigned,const uint32_t *,size_t) override;
  Words download(unsigned, unsigned) override;
  bo_run run(unsigned, unsigned, std::array<unsigned, 2>, unsigned bank=2) override;
  void present(unsigned, unsigned, unsigned, const std::array<int32_t, 180> &) override;
  unsigned publish(unsigned,unsigned,unsigned,const std::array<int32_t,180> &,const Words &) override;
  J publish_stats() override;
  void pose(const std::array<int32_t, 180> &) override;
  void stop() override;
  void batch(const std::vector<bo_batch_entry> &) override;
  void batch_view(const bo_batch_entry *,size_t) override;
  void blend(const bo_tile_blend &) override;
  bool copy(unsigned,unsigned,unsigned,unsigned,size_t) override;
  bool fill(unsigned,unsigned,size_t,unsigned) override;
  J dma_status() override;
  bool rgb_fill(uint8_t *, size_t, const std::array<uint8_t,3> &) override;
};
struct MockDevice : Device {
  std::array<Words, 6> regions;
  int active = -1;
  unsigned frames = 0;
  Words active_metadata;
  unsigned active_words=0;
  J trace = J::array();
  bool poisoned = false;
  int fail_after = -1;
  bool dma_enabled = true;
  uint64_t dma_copies = 0, dma_fills = 0;
  MockDevice();
  void check();
  bo_info info() override;
  void upload(unsigned, unsigned, const uint32_t *, size_t) override;
  void upload_direct(unsigned,unsigned,const uint32_t *,size_t) override;
  Words download(unsigned, unsigned) override;
  bo_run run(unsigned, unsigned, std::array<unsigned, 2>, unsigned bank=2) override;
  void present(unsigned, unsigned, unsigned, const std::array<int32_t, 180> &) override;
  unsigned publish(unsigned,unsigned,unsigned,const std::array<int32_t,180> &,const Words &) override;
  J publish_stats() override;
  void pose(const std::array<int32_t, 180> &) override;
  void stop() override;
  void batch(const std::vector<bo_batch_entry> &) override;
  void blend(const bo_tile_blend &) override;
  bool copy(unsigned,unsigned,unsigned,unsigned,size_t) override;
  bool fill(unsigned,unsigned,size_t,unsigned) override;
  J dma_status() override;
  bool rgb_fill(uint8_t *, size_t, const std::array<uint8_t,3> &) override;
};
struct Output {
  Device &dev;
  const Geometry &geometry;
  int active = -1;
  bool failed = false;
  bool blank_only = false;
  uint64_t commits = 0, uploaded_words = 0, shadow_copied_words = 0;
  uint64_t dma_copied_words = 0;
  Words shadow;
  std::array<int32_t, 180> camera;
  Output(Device &d, const Geometry &g)
      : dev(d), geometry(g), active(d.info().active_bank), camera(g.camera()) {}
  struct Range {
    unsigned offset;
    const uint32_t *data;
    size_t count;
  };
  void commit(size_t scene_size, const std::vector<Range> &ranges);
  void full(const Words &scene, bool fixed_active_only = false);
  void initialize_blank();
  void patch(const Words &patch);
  void set_pose(double yaw, double pitch);
  J status();
};
struct Projectors {
  Device &dev;
  bool failed = false;
  std::shared_ptr<const Texture> resident_texture;
  uint64_t texture_uploads = 0, texture_reuses = 0;
  explicit Projectors(Device &d) : dev(d) {}
  bo_run run(WordView texture, const std::vector<Descriptor> &desc, Words &scene,
             std::shared_ptr<const Texture> immutable = {});
};
struct Layer {
  uint64_t layout_version=0;
  std::vector<uint64_t> tile_content_revision;
  J key, shape;
  uint64_t revision = 0;
  std::vector<unsigned> ids;
  std::vector<Descriptor> desc;
  std::vector<Bytes> masks;
  std::vector<std::array<uint32_t,32>> packed_masks;
  const uint32_t *packed_mask(unsigned i) const{return plan?plan->packed_masks[i].data():packed_masks[i].data();}
  std::vector<unsigned> mask_kinds;
  std::shared_ptr<const ProjectionPlan> plan;
  const uint8_t *mask(unsigned i) const{return plan?plan->masks[i]:masks[i].data();}
  Words pixels, source;
  std::shared_ptr<const Texture> prepared_source;
  std::set<unsigned> invalid;
  std::array<int, tiles> lookup;
  Layer() { lookup.fill(-1); }
};
struct Composition {
  Words words;
  // Borrowed only until the next Compositor::update; the worker consumes it synchronously.
  const Words *scene_view = nullptr;
  const Words &data() const { return scene_view ? *scene_view : words; }
  unsigned changed = 0;
  bool patch = false;
};
struct Compositor {
  const Geometry &geometry;
  Projectors &projectors;
  std::map<int, Layer> layers;
  Words scene;
  bool valid = false;
  std::vector<int> previous_order;
  std::map<int, std::set<unsigned>> previous_tiles;
  std::vector<unsigned> pointer_outer, pointer_inner;
  std::set<unsigned> previous_pointer_tiles;
  J pointer_key, metrics;
  int previous_bg = -1;
  uint64_t total_jobs = 0, frames = 0;
  uint64_t geometry_tiles_evaluated=0,mask_samples_evaluated=0,precomputed_layers=0;
  std::unique_ptr<NativeRenderer> fallback;
  uint64_t fallback_frames = 0;
  bool last_hardware = false;
  bool resident_only=false;
  Compositor(const Geometry &g, Projectors &p, bool defer_scene = false, bool resident=false);
  void allocate_scene();
  Layer make_layer(const Window &w);
  void prepare_masks(Layer &c,const Window &w);
  J export_plan(const Window &w,const std::string &path);
  void source_changes(Layer &c, const Window &w);
  Composition update(WM &wm, bool full = false);
  void ack() { valid = last_hardware; }
  void invalidate() { valid = false; }
};
} // namespace boayo
