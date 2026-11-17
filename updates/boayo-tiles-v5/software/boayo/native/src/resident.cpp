#include "resident.hpp"
#include "frame_trace.hpp"
#include <sys/ioctl.h>
namespace boayo {
unsigned Device::publish(unsigned bank,unsigned count,unsigned res,const std::array<int32_t,180> &camera,const Words &metadata){
  require(metadata.size()>=header,"publish metadata length");upload(3+bank,0,metadata.data(),header);present(bank,count,res,camera);return header;
}
unsigned LinuxDevice::publish(unsigned bank,unsigned count,unsigned res,const std::array<int32_t,180> &camera,const Words &metadata){
  require(dma_features&BO_DMA_PUBLISH,"metadata publish driver required");require(metadata.size()>=header,"publish metadata length");
  bo_publish p{};p.present.bank=bank;p.present.scene_words=count;p.present.resolution=res;p.present.timeout_ms=30000;
  std::copy(camera.begin(),camera.end(),p.present.camera);p.words=header;p.metadata=reinterpret_cast<uintptr_t>(metadata.data());
  require(ioctl(fd,BO_PUBLISH,&p)==0,"metadata publish ioctl failed");last_publish=p.profile;
  frame_trace::mark("publish.user_copy_us",p.profile.user_copy_us);
  frame_trace::mark("publish.validate_us",p.profile.validate_us);
  frame_trace::mark("publish.upload_us",p.profile.upload_us);
  frame_trace::mark("publish.configure_us",p.profile.configure_us);
  frame_trace::mark("publish.select_us",p.profile.select_us);
  frame_trace::mark("publish.wait_us",p.profile.wait_us);
  frame_trace::mark("publish.finish_us",p.profile.finish_us);
  return p.profile.uploaded_words;
}
J LinuxDevice::publish_stats(){return {{"backend","kernel"},{"user_copy_us",last_publish.user_copy_us},{"validate_us",last_publish.validate_us},
  {"upload_us",last_publish.upload_us},{"configure_us",last_publish.configure_us},{"select_us",last_publish.select_us},
  {"wait_us",last_publish.wait_us},{"finish_us",last_publish.finish_us},{"uploaded_words",last_publish.uploaded_words},{"upload_ranges",last_publish.upload_ranges},{"command",last_publish.command}};}
unsigned MockDevice::publish(unsigned bank,unsigned count,unsigned res,const std::array<int32_t,180> &camera,const Words &metadata){
  check();require(bank<2 && int(bank)!=active && metadata.size()>=header,"mock publish bounds");
  unsigned words=0;trace.push_back({{"op","publish"},{"bank",bank}});
  for(unsigned i=0;i<header;){
    if(regions[3+bank][i]==metadata[i]){++i;continue;}unsigned first=i++;
    while(i<header && regions[3+bank][i]!=metadata[i])++i;
    upload(3+bank,first,metadata.data()+first,i-first);words+=i-first;
  }
  present(bank,count,res,camera);return words;
}
J MockDevice::publish_stats(){return {{"backend","mock"}};}
// The fallback preserves the scalar API for other Device implementations.
void Device::batch(const std::vector<bo_batch_entry> &entries){
  for(auto &b:entries){
    if(b.kind==BO_BATCH_COPY){auto &c=b.op.copy;require(copy(c.src_region,c.src_offset/4,c.dst_region,c.dst_offset/4,c.bytes/4),"batch copy required");}
    else if(b.kind==BO_BATCH_FILL){auto &f=b.op.fill;require(fill(f.region,f.offset/4,f.bytes/4,f.value),"batch fill required");}
    else {require(b.kind==BO_BATCH_BLEND,"batch kind");blend(b.op.blend);}
  }
}
void LinuxDevice::batch(const std::vector<bo_batch_entry> &entries){
  batch_view(entries.data(),entries.size());
}
void LinuxDevice::batch_view(const bo_batch_entry *entries,size_t count){
  require(dma_features&BO_DMA_BATCH,"batch driver required");
  for(size_t first=0;first<count;first+=BO_BATCH_MAX){bo_batch b{};
    b.count=std::min(size_t(BO_BATCH_MAX),count-first);
    b.entries=reinterpret_cast<uintptr_t>(entries+first);
    require(ioctl(fd,BO_BATCH,&b)==0,"batch ioctl failed");
  }
}
void MockDevice::batch(const std::vector<bo_batch_entry> &entries){
  for(size_t first=0;first<entries.size();first+=BO_BATCH_MAX){
    size_t n=std::min(size_t(BO_BATCH_MAX),entries.size()-first);
    trace.push_back({{"op","batch"},{"count",n}});
    Device::batch(std::vector<bo_batch_entry>(entries.begin()+first,entries.begin()+first+n));
  }
}
static void queue_fill(std::vector<bo_batch_entry> &list,unsigned bank,unsigned tile,unsigned color){
  bo_batch_entry b{};b.kind=BO_BATCH_FILL;b.op.fill={3+bank,(header+tile*256)*4,1024,color,0};list.push_back(b);
}
static void queue_blend(std::vector<bo_batch_entry> &list,const bo_tile_blend &blend){
  bo_batch_entry b{};b.kind=BO_BATCH_BLEND;b.op.blend=blend;list.push_back(b);
}
void LinuxDevice::blend(const bo_tile_blend &b){
  require(dma_features&BO_DMA_TILE_BLEND,"tile blend driver required");
  auto command=b;require(ioctl(fd,BO_TILE_BLEND,&command)==0,"tile blend ioctl failed");
}
void MockDevice::blend(const bo_tile_blend &b){
  check();require(b.bank<2 && int(b.bank)!=active && b.tile<tiles && b.mode<=1 && b.color<256 && !b.reserved,"mock blend bounds");
  require(b.mode || (b.scratch_word>=header && b.scratch_word+256<=regions[5].size()),"mock blend source");
  auto *dst=reinterpret_cast<uint8_t *>(regions[3+b.bank].data()+header+b.tile*256);
  const auto *src=reinterpret_cast<const uint8_t *>(regions[5].data()+b.scratch_word);
  for(unsigned i=0;i<1024;++i)if(b.mask[i/32]&(1u<<(i%32)))dst[i]=b.mode?b.color:src[i];
  trace.push_back({{"op","tile_blend"},{"bank",b.bank},{"tile",b.tile},{"mode",b.mode}});
}
bool ResidentCompositor::supported(const WM &wm){
  for(auto &[id,w]:wm.windows)if(w.data["mapped"].get<bool>() &&
    (w.data["decorated"].get<bool>() || w.data["width_deg"].get<double>()>=160 ||
     w.data["surface_width"].get<unsigned>()>2048 || w.data["surface_height"].get<unsigned>()>2048))return false;
  return true;
}
static J key(const Window &w){auto &d=w.data;return J::array({d["surface_width"],d["surface_height"],d["azimuth"],d["elevation"],d["roll"],d["width_deg"],d["height_deg"]});}
void ResidentCompositor::execute(Layer &c,const std::vector<unsigned> &todo,unsigned bank,bool direct,std::array<unsigned,2> &jobs){
  for(size_t first=0;first<todo.size();first+=128){
    unsigned n=std::min(size_t(128),todo.size()-first);std::array<Words,2> queues;
    for(unsigned i=0;i<n;++i){unsigned index=todo[first+i];auto d=c.desc[index];
      d[1]=header+(direct?c.ids[index]:i)*256;
      // Full tile overwrites all cells. Coverage-only boundary preserves the
      // existing lower layers; a shaped boundary uses scratch plus a stencil.
      d[5]=direct?(c.mask_kinds[index]==1?1:0):1;
      queues[i%2].insert(queues[i%2].end(),d.begin(),d.end());
    }
    std::array<unsigned,2> counts{};
    for(unsigned core=0;core<2;++core){counts[core]=queues[core].size()/18;if(counts[core])dev.upload(1+core,0,queues[core].data(),queues[core].size());}
    auto texture=c.prepared_source?WordView(*c.prepared_source):WordView(c.source);
    auto done=dev.run(direct?scene_words:((header+n*256+15)&~15u),texture.size(),counts,direct?bank:2);
    require(!done.status[0]&&!done.status[1],"resident projection error");
    for(unsigned core=0;core<2;++core){jobs[core]+=counts[core];total_jobs+=counts[core];}
    std::vector<bo_batch_entry> blends;
    if(!direct)for(unsigned i=0;i<n;++i){unsigned index=todo[first+i];bo_tile_blend b{};
      b.bank=bank;b.tile=c.ids[index];b.scratch_word=header+i*256;
      std::copy_n(c.packed_mask(index),32,b.mask);++packed_mask_tiles;
      queue_blend(blends,b);++blended_tiles;
    }
    dev.batch(blends);
  }
}
bool ResidentCompositor::update(WM &wm){
  require(supported(wm),"unsupported resident window");require(!output.failed,"output failed; restart required");
  auto &p=planner;std::vector<int> order;for(int id:wm.order)if(wm.windows.at(id).data["mapped"].get<bool>())order.push_back(id);
  std::set<unsigned> dirty;
  for(int id:p.previous_order)if(std::find(order.begin(),order.end(),id)==order.end())dirty.insert(p.previous_tiles[id].begin(),p.previous_tiles[id].end());
  for(auto i=p.layers.begin();i!=p.layers.end();)if(!wm.windows.count(i->first))i=p.layers.erase(i);else ++i;
  for(int id:order){auto &w=wm.windows.at(id);auto found=p.layers.find(id);
    bool fresh=found==p.layers.end() || found->second.key!=key(w);
    if(fresh){dirty.insert(p.previous_tiles[id].begin(),p.previous_tiles[id].end());auto c=p.make_layer(w);
      if(found!=p.layers.end()){c.prepared_source=found->second.prepared_source;c.source=std::move(found->second.source);c.revision=found->second.revision;}
      c.layout_version=generation+1;
      p.layers[id]=std::move(c);dirty.insert(p.layers[id].ids.begin(),p.layers[id].ids.end());}
    auto &c=p.layers[id];
    if(c.shape!=w.data["shape"]){c.layout_version=generation+1;p.prepare_masks(c,w);dirty.insert(c.ids.begin(),c.ids.end());}
    p.source_changes(c,w);
    c.tile_content_revision.resize(c.ids.size());
    for(unsigned index:c.invalid)c.tile_content_revision[index]=generation+1;
    for(auto index:c.invalid)if(c.mask_kinds[index])dirty.insert(c.ids[index]);
    if(!valid || std::find(p.previous_order.begin(),p.previous_order.end(),id)==p.previous_order.end())dirty.insert(c.ids.begin(),c.ids.end());
  }
  // Reordering only touches the union of windows, never the whole sphere.
  if(order!=p.previous_order)for(int id:order){auto &c=p.layers[id];dirty.insert(c.ids.begin(),c.ids.end());}
  J pk=wm.pointer_visible?J::array({wm.paz,wm.pel,true}):J::array({false});
  if(pk!=p.pointer_key){
    pointer_version=generation+1;
    dirty.insert(p.previous_pointer_tiles.begin(),p.previous_pointer_tiles.end());p.pointer_outer.clear();p.pointer_inner.clear();
    if(wm.pointer_visible){V direction=axes(wm.paz,wm.pel)[0];for(auto &x:direction)x=float(x);
      for(unsigned t=0;t<tiles;++t){auto tri=p.geometry.triangles[t];V center=unit((tri[0]+tri[1]+tri[2])*(1./3));double min_dot=1;
        for(auto q:tri)min_dot=std::min(min_dot,dot(unit(q),center));
        double threshold=std::cos(std::acos(std::clamp(min_dot,-1.,1.))+rad(2.2)+1e-6);
        if(dot(center,direction)<threshold)continue;
        for(unsigned cell=0;cell<1024;++cell){V q=p.geometry.ray(t,cell);for(auto &x:q)x=float(x);
          float d=float(float(float(q[0]*direction[0])+float(q[1]*direction[1]))+float(q[2]*direction[2]));
          if(d>=float(std::cos(rad(2.2))))p.pointer_outer.push_back(t*1024+cell);
          if(d>=float(std::cos(rad(1.))))p.pointer_inner.push_back(t*1024+cell);
        }
      }
    }
    p.previous_pointer_tiles.clear();for(auto x:p.pointer_outer)p.previous_pointer_tiles.insert(x/1024);for(auto x:p.pointer_inner)p.previous_pointer_tiles.insert(x/1024);
    dirty.insert(p.previous_pointer_tiles.begin(),p.previous_pointer_tiles.end());p.pointer_key=pk;
  }
  if(!valid)dirty.insert(p.previous_pointer_tiles.begin(),p.previous_pointer_tiles.end());
  bool any=!dirty.empty();unsigned bank=output.active<0?0:unsigned(output.active)^1;std::array<unsigned,2> jobs{};
  std::set<unsigned> redraw;std::map<unsigned,std::vector<uint64_t>> new_keys;bool scene_changed=!valid;
  try{
    // Only the directory/palette are represented on the CPU.
    output.shadow.resize((header+15)&~15u);
    if(!valid)std::fill(output.shadow.begin()+256,output.shadow.begin()+header,0xffffffffu);

    ++generation;
    bool ready=!valid && frames==0 && order.size()==1 && !wm.pointer_visible;
    if(ready){auto &c=p.layers.at(order.front());auto &w=wm.windows.at(order.front());
      ready=c.plan && c.plan->ready && c.prepared_source && w.solid_revision!=w.revision && !c.shape.empty();}
    if(ready){
      auto id=order.front();auto &c=p.layers.at(id);auto r=c.plan->ready->data();
      unsigned masked=r[6]+r[7];auto texture=WordView(*c.prepared_source);
      frame_trace::mark("ready.prepare.begin");
      // All descriptors, DMA fill/blend requests and directory entries are
      // immutable device-format data. Only the destination bank is selected.
      std::copy_n(r+r[16],tiles,output.shadow.begin()+256);
      for(unsigned i=0;i<c.ids.size();++i)if(c.mask_kinds[i]){
        unsigned t=c.ids[i];wanted[t]=generation;stored[bank][t]=0;redraw.insert(t);
        new_keys.emplace(t,std::vector<uint64_t>{uint64_t(id),c.layout_version,c.tile_content_revision[i],0});
      }
      if(masked)dev.batch_view(reinterpret_cast<const bo_batch_entry *>(r+r[12+bank]),masked);
      cleared_tiles+=masked;frame_trace::mark("ready.prepare.end",redraw.size());
      frame_trace::mark("texture.upload.begin",texture.size());
      dev.upload_direct(0,0,texture.data(),texture.size());
      p.projectors.resident_texture=c.prepared_source;++p.projectors.texture_uploads;
      frame_trace::mark("texture.upload.end",texture.size());
      frame_trace::mark("projectors.begin",redraw.size());
      for(unsigned phase=0;phase<2;++phase){std::array<unsigned,2> counts{r[4+phase*2],r[5+phase*2]};
        if(!counts[0]&&!counts[1])continue;
        for(unsigned core=0;core<2;++core)if(counts[core])dev.upload(1+core,0,r+r[8+phase*2+core],counts[core]*18);
        auto done=dev.run(phase?((header+masked*256+15)&~15u):scene_words,texture.size(),counts,phase?2:bank);
        require(!done.status[0]&&!done.status[1],"resident projection error");
        for(unsigned core=0;core<2;++core){jobs[core]+=counts[core];total_jobs+=counts[core];}
      }
      if(masked)dev.batch_view(reinterpret_cast<const bo_batch_entry *>(r+r[14+bank]),masked);
      packed_mask_tiles+=masked;blended_tiles+=masked;++ready_frames;
      frame_trace::mark("projectors.end",redraw.size());
    }else{
    frame_trace::mark("tiles.prepare.begin",dirty.size());
    std::vector<bo_batch_entry> prepare;
    for(unsigned t:dirty){bool occupied=false,first_full=false;
      for(int id:order){auto &c=p.layers[id];int i=c.lookup[t];if(i>=0 && c.mask_kinds[i]){occupied=true;first_full=c.mask_kinds[i]==1;break;}}
      bool pointer=p.previous_pointer_tiles.count(t);
      unsigned entry=(occupied||pointer)?t*1024:0xffffffffu;
      scene_changed|=output.shadow[256+t]!=entry;output.shadow[256+t]=entry;
      if(!occupied && !pointer){wanted[t]=0;continue;}
      // Exact input identities, not hashes or cell readback. Preserve old slot
      // identities through deactivation so an unchanged reactivation can reuse.
      std::vector<uint64_t> identity;identity.reserve(order.size()*3+1);
      for(int id:order){auto &c=p.layers[id];int i=c.lookup[t];
        if(i>=0 && c.mask_kinds[i]){identity.push_back(uint64_t(id));
          identity.push_back(c.layout_version);identity.push_back(c.tile_content_revision[i]);}}
      identity.push_back(pointer?pointer_version:0);
      unsigned source=bank;
      if(!stored[source][t] || tile_keys[source][t]!=identity)source^=1;
      if(stored[source][t] && tile_keys[source][t]==identity){wanted[t]=stored[source][t];scene_changed|=output.active<0 || stored[output.active][t]!=wanted[t];++reused_tiles;continue;}
      scene_changed=true;wanted[t]=generation;redraw.insert(t);new_keys.emplace(t,std::move(identity));
      stored[bank][t]=0; // A failed or partial render must never be reused.
      if(!first_full){queue_fill(prepare,bank,t,0);++cleared_tiles;}
    }
    any=scene_changed;
    // Unchanged active tiles may be stale in the next back bank. Copy only
    // missing versions, before presentation. Never touch inactive cell slots.
    if(any || !valid)for(unsigned t=0;t<tiles;){
      if(!wanted[t] || redraw.count(t) || stored[bank][t]==wanted[t]){++t;continue;}
      unsigned first=t,source=bank^1;
      do {
        require(stored[source][t]==wanted[t],"missing resident tile generation");++t;
      }while(t<tiles && wanted[t] && !redraw.count(t) && stored[bank][t]!=wanted[t]);
      bo_batch_entry copy{};copy.kind=BO_BATCH_COPY;
      copy.op.copy={3+source,(header+first*256)*4,3+bank,(header+first*256)*4,(t-first)*1024,0};prepare.push_back(copy);
      output.dma_copied_words+=(t-first)*256;lazy_copied_tiles+=t-first;
      for(unsigned j=first;j<t;++j){stored[bank][j]=wanted[j];tile_keys[bank][j]=tile_keys[source][j];}
    }
    dev.batch(prepare);
    frame_trace::mark("tiles.prepare.end",dirty.size());
    for(int id:order){auto &c=p.layers[id];std::vector<unsigned> direct,masked;std::vector<bo_batch_entry> solids;
      for(unsigned i=0;i<c.ids.size();++i)if(redraw.count(c.ids[i])&&c.mask_kinds[i]){
        if(c.mask_kinds[i]==1 && wm.windows.at(id).solid_revision==wm.windows.at(id).revision){
          queue_fill(solids,bank,c.ids[i],wm.windows.at(id).solid_index);++solid_tiles;
        }else if(c.mask_kinds[i]==1 || c.shape.empty())direct.push_back(i);else masked.push_back(i);}
      dev.batch(solids);
      if(direct.empty()&&masked.empty())continue;
      auto texture=c.prepared_source?WordView(*c.prepared_source):WordView(c.source);auto &pr=p.projectors;
      if(!c.prepared_source || pr.resident_texture!=c.prepared_source){pr.resident_texture.reset();frame_trace::mark("texture.upload.begin",texture.size());
        dev.upload_direct(0,0,texture.data(),texture.size());pr.resident_texture=c.prepared_source;++pr.texture_uploads;frame_trace::mark("texture.upload.end",texture.size());
      }else ++pr.texture_reuses;
      frame_trace::mark("projectors.begin",direct.size()+masked.size());execute(c,direct,bank,true,jobs);execute(c,masked,bank,false,jobs);frame_trace::mark("projectors.end",direct.size()+masked.size());
    }
    }
    for(unsigned pass=0;pass<2;++pass){const auto &pixels=pass?p.pointer_inner:p.pointer_outer;
      std::map<unsigned,bo_tile_blend> paint;
      for(unsigned x:pixels)if(redraw.count(x/1024)){auto &b=paint[x/1024];b.bank=bank;b.tile=x/1024;b.mode=1;b.color=pass?255:rgb_index(10,10,10);b.mask[(x%1024)/32]|=1u<<(x%32);}
      std::vector<bo_batch_entry> paints;for(auto &[t,b]:paint)queue_blend(paints,b);dev.batch(paints);
    }
    if(any || !valid){frame_trace::mark("output.upload.begin");
      frame_trace::mark("output.versions.begin");
      // Record a version only once all ordered layers and pointer writes finish.
      for(unsigned t:redraw){stored[bank][t]=wanted[t];tile_keys[bank][t]=std::move(new_keys.at(t));}
      // Ordered device calls above return only after completion. Full scene
      // audits are opt-in diagnostics, not work repeated for every presentation.
      const char *audit=getenv("BOAYO_VALIDATE_SCENE");
      if(audit && std::string(audit)=="1")for(unsigned t=0;t<tiles;++t)
        if(wanted[t] && stored[bank][t]!=wanted[t])throw std::runtime_error("incomplete scene");
      frame_trace::mark("output.versions.end");
      frame_trace::mark("output.publish.begin");
      output.uploaded_words+=dev.publish(bank,scene_words,2,output.camera,output.shadow);
      frame_trace::mark("output.publish.end");output.active=bank;++output.commits;frame_trace::mark("output.present.done");
      output.blank_only=false;frame_trace::mark("output.upload.end");
    }
  }catch(...){output.failed=true;p.projectors.resident_texture.reset();throw;}
  for(int id:order){auto &c=p.layers[id];c.invalid.clear();wm.windows[id].dirty={};p.previous_tiles[id]={c.ids.begin(),c.ids.end()};}
  p.previous_order=order;valid=true;++frames;
  metrics={{"device_ready_frames",ready_frames},{"packed_mask_tiles_total",packed_mask_tiles},{"redrawn_tiles",redraw.size()},{"reused_tiles_total",reused_tiles},{"lazy_copied_tiles_total",lazy_copied_tiles},{"solid_tiles_total",solid_tiles},{"mode","ddr-resident-dirty-tiles"},{"hardware_frames",frames},{"fallback_frames",0},
    {"geometry_tiles_evaluated_total",p.geometry_tiles_evaluated},{"mask_samples_evaluated_total",p.mask_samples_evaluated},
    {"precomputed_layers_total",p.precomputed_layers},{"plan_cache",projection_plan_stats()},
    {"jobs_per_core",jobs},{"total_hw_jobs",total_jobs},{"dirty_tiles",dirty.size()},
    {"cpu_scene_words",p.scene.size()},{"cpu_output_shadow_words",output.shadow.size()},
    {"cpu_result_download_words",0},{"tile_fills_total",cleared_tiles},{"masked_tiles_total",blended_tiles},
    {"texture_uploads_total",p.projectors.texture_uploads},{"texture_reuses_total",p.projectors.texture_reuses},
    {"update_kind",any?"tiles":"none"}};
  return any;
}
}
