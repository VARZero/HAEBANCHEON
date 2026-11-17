#include "resident.hpp"
#include "frame_trace.hpp"
#include <sys/ioctl.h>
namespace boayo {
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
    if(!direct)for(unsigned i=0;i<n;++i){unsigned index=todo[first+i];bo_tile_blend b{};
      b.bank=bank;b.tile=c.ids[index];b.scratch_word=header+i*256;
      const uint8_t *mask=c.mask(index);
      for(unsigned cell=0;cell<1024;++cell)if(mask[cell])b.mask[cell/32]|=1u<<(cell%32);
      dev.blend(b);++blended_tiles;
    }
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
      p.layers[id]=std::move(c);dirty.insert(p.layers[id].ids.begin(),p.layers[id].ids.end());}
    auto &c=p.layers[id];
    if(c.shape!=w.data["shape"]){p.prepare_masks(c,w);dirty.insert(c.ids.begin(),c.ids.end());}
    p.source_changes(c,w);for(auto index:c.invalid)if(c.mask_kinds[index])dirty.insert(c.ids[index]);
    if(!valid || std::find(p.previous_order.begin(),p.previous_order.end(),id)==p.previous_order.end())dirty.insert(c.ids.begin(),c.ids.end());
  }
  // Reordering only touches the union of windows, never the whole sphere.
  if(order!=p.previous_order)for(int id:order){auto &c=p.layers[id];dirty.insert(c.ids.begin(),c.ids.end());}
  J pk=wm.pointer_visible?J::array({wm.paz,wm.pel,true}):J::array({false});
  if(pk!=p.pointer_key){
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
  try{
    // Only the directory/palette are represented on the CPU.
    output.shadow.resize((header+15)&~15u);
    if(!valid)std::fill(output.shadow.begin()+256,output.shadow.begin()+header,0xffffffffu);
    frame_trace::mark("tiles.prepare.begin",dirty.size());
    for(unsigned t:dirty){bool occupied=false,first_full=false;
      for(int id:order){auto &c=p.layers[id];int i=c.lookup[t];if(i>=0 && c.mask_kinds[i]){occupied=true;first_full=c.mask_kinds[i]==1;break;}}
      bool pointer=p.previous_pointer_tiles.count(t);
      output.shadow[256+t]=(occupied||pointer)?t*1024:0xffffffffu;
      if((occupied||pointer)&&!first_full){require(dev.fill(3+bank,header+t*256,256,0),"tile DMA fill required");++cleared_tiles;}
    }
    frame_trace::mark("tiles.prepare.end",dirty.size());
    for(int id:order){auto &c=p.layers[id];std::vector<unsigned> direct,masked;
      for(unsigned i=0;i<c.ids.size();++i)if(dirty.count(c.ids[i])&&c.mask_kinds[i]){
        if(c.mask_kinds[i]==1 || c.shape.empty())direct.push_back(i);else masked.push_back(i);}
      if(direct.empty()&&masked.empty())continue;
      auto texture=c.prepared_source?WordView(*c.prepared_source):WordView(c.source);auto &pr=p.projectors;
      if(!c.prepared_source || pr.resident_texture!=c.prepared_source){pr.resident_texture.reset();frame_trace::mark("texture.upload.begin",texture.size());
        dev.upload_direct(0,0,texture.data(),texture.size());pr.resident_texture=c.prepared_source;++pr.texture_uploads;frame_trace::mark("texture.upload.end",texture.size());
      }else ++pr.texture_reuses;
      frame_trace::mark("projectors.begin",direct.size()+masked.size());execute(c,direct,bank,true,jobs);execute(c,masked,bank,false,jobs);frame_trace::mark("projectors.end",direct.size()+masked.size());
    }
    for(unsigned pass=0;pass<2;++pass){const auto &pixels=pass?p.pointer_inner:p.pointer_outer;
      std::map<unsigned,bo_tile_blend> paint;
      for(unsigned x:pixels)if(dirty.count(x/1024)){auto &b=paint[x/1024];b.bank=bank;b.tile=x/1024;b.mode=1;b.color=pass?255:rgb_index(10,10,10);b.mask[(x%1024)/32]|=1u<<(x%32);}
      for(auto &[t,b]:paint)dev.blend(b);
    }
    if(any || !valid){frame_trace::mark("output.upload.begin");
      dev.upload(3+bank,256,output.shadow.data()+256,header-256);output.uploaded_words+=header-256;
      dev.present(bank,scene_words,2,output.camera);output.active=bank;++output.commits;frame_trace::mark("output.present.done");
      // Mirror only modified resident tiles so the next back bank starts valid.
      for(auto i=dirty.begin();i!=dirty.end();){unsigned first=*i,last=first;
        if(output.shadow[256+first]==0xffffffffu){++i;continue;}++i;
        while(i!=dirty.end() && *i==last+1 && output.shadow[256+*i]!=0xffffffffu){last=*i;++i;}
        size_t count=(last-first+1)*256;require(dev.copy(3+bank,header+first*256,3+(bank^1),header+first*256,count),"tile mirror DMA required");output.dma_copied_words+=count;
      }
      require(dev.copy(3+bank,256,3+(bank^1),256,header-256),"directory mirror DMA required");output.dma_copied_words+=header-256;
      output.blank_only=false;frame_trace::mark("output.upload.end");
    }
  }catch(...){output.failed=true;p.projectors.resident_texture.reset();throw;}
  for(int id:order){auto &c=p.layers[id];c.invalid.clear();wm.windows[id].dirty={};p.previous_tiles[id]={c.ids.begin(),c.ids.end()};}
  p.previous_order=order;valid=true;++frames;
  metrics={{"mode","ddr-resident-dirty-tiles"},{"hardware_frames",frames},{"fallback_frames",0},
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
