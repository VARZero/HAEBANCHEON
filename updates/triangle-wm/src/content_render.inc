 unsigned scene_size()const{return (header+(ui_enabled?tiles:std::max(next_slot,1u))*256+15)&~15u;}
 void upload(unsigned region,unsigned offset,const uint32_t*data,unsigned words){bo_upload u{};u.region=region;u.offset=offset*4;u.bytes=words*4;u.data=uintptr_t(data);call(BO_UPLOAD,&u);}
 void prepare(Window&w){
  if(!w.has_content||w.plan_ready)return;
  double started=now();
  w.transform.update(w.yaw,w.pitch,w.width,w.height,w.padding);
  auto projection=w.transform.projection(w.surface.width,w.surface.height);
  FastProjection transform(projection);
  w.plan.clear();w.plan.reserve(w.footprint.size());for(unsigned t:w.footprint){if(!transform.candidate(t)){plan_rejected++;continue;}auto [d,visible]=fast_descriptor(geometry,t,projection,transform,plan_fallbacks);if(visible)w.plan.push_back({t,{d,++serial}});}
  w.plan_ready=true;plan_tiles+=w.footprint.size();plan_ms+=(now()-started)*1000;
 }
 void damage(Window&w,DamageRect r){if(!r.w||!r.h)return;for(auto&[t,p]:w.plan)if(affected(p.descriptor,r))p.version=++serial;}
 J render_report()const{
  const auto &s=stats;
  if(!s.published)return json_object("changed_tiles",0,"owned_tiles",s.owned,"projected_tiles",0,"copied_tiles",0,"published",false,"render_ms",s.render_ms);
  return json_object("plan_ms",s.plan_ms,"plan_tiles",s.plan_tiles,"plan_rejected",s.plan_rejected,"plan_fallbacks",s.plan_fallbacks,
   "changed_tiles",s.changed,"content_changed_tiles",s.content_changed,"owned_tiles",s.owned,"projected_tiles",s.projected,"copied_tiles",s.copied,
   "jobs_per_core",s.jobs,"texture_upload_bytes",s.uploaded,"texture_scatter_bytes",s.scatter,"texture_evictions",s.evictions,"resident_textures",s.resident,
   "projection_ms",s.projection_ms,"published",true,"render_ms",s.render_ms,"publish_ms",s.publish_ms,"validate_ms",s.profile.validate_us/1000.,
   "metadata_ms",s.profile.upload_us/1000.,"wait_ms",s.profile.wait_us/1000.,"active_bank",s.bank,"ui_ms",ui_ms,"overlay_tiles",ui_tile_count);
 }
 J render(bool describe=true){
  double begin=now();stats={};plan_ms=0;texture_cache.evictions=0;plan_tiles=plan_fallbacks=plan_rejected=0;auto &next=scratch_metadata;next=metadata;auto&owner=planned_owner;auto&wanted=planned_version;auto &touched=scratch_touched;auto &new_visible=scratch_visible;touched=visible_tiles;new_visible.clear();
  for(unsigned t:touched){owner[t]=0;wanted[t]=0;next[256+t]=UINT32_MAX;touched_flag[t]=1;}
  for(int id:order){auto&w=windows.at(id);if(w.visible){for(unsigned t:w.footprint){if(!touched_flag[t]){touched.push_back(t);touched_flag[t]=1;wanted[t]=0;}if(!owner[t])new_visible.push_back(t);owner[t]=id;}}}
  for(int id:order){auto &w=windows.at(id);if(w.visible && w.has_content && !w.plan_ready && std::any_of(w.footprint.begin(),w.footprint.end(),[&](unsigned t){return owner[t]==id;}))prepare(w);}
  unsigned owned=new_visible.size();for(unsigned t:new_visible){auto&w=windows.at(owner[t]);next[256+t]=colors.at(w.color)*1024;if(w.has_content){auto p=w.plan.find(t);if(p!=w.plan.end())wanted[t]=p->second.version;}}
  for(unsigned t:touched)if(!wanted[t]&&slots[t]>=0){free_slots.push_back(slots[t]);slots[t]=-1;versions[0][t]=versions[1][t]=0;}
  for(unsigned t:new_visible)if(wanted[t]){if(slots[t]<0){if(!free_slots.empty()){slots[t]=free_slots.back();free_slots.pop_back();}else{require(next_slot<(ui_enabled?tiles-256:tiles),"scene payload capacity");slots[t]=next_slot++;}versions[0][t]=versions[1][t]=0;}next[256+t]=unsigned(slots[t])*1024;}
  unsigned changed=0,content_changed=0;for(unsigned t:touched){changed+=next[256+t]!=metadata[256+t]||wanted[t]!=presented[t];content_changed+=wanted[t]&&wanted[t]!=presented[t];}
  visible_tiles.swap(new_visible);for(unsigned t:touched)touched_flag[t]=0;
  if(active>=0&&!changed&&!(ui_enabled&&ui_dirty)){stats.owned=owned;stats.render_ms=(now()-begin)*1000;return describe?render_report():J();}
  unsigned bank=active<0?0:unsigned(active)^1;auto &copies=copy_batch;copies.clear();std::map<int,std::vector<unsigned>> project;unsigned copied=0,projected=0,uploaded=0,scatter_bytes=0;std::array<unsigned,2> jobs{};
  for(unsigned t:visible_tiles)if(wanted[t]&&versions[bank][t]!=wanted[t]){
   if(active>=0&&versions[active][t]==wanted[t]){bo_batch_entry e{};e.kind=BO_BATCH_COPY;e.op.copy={BO_SCENE0+unsigned(active),(header+unsigned(slots[t])*256)*4,BO_SCENE0+bank,(header+unsigned(slots[t])*256)*4,1024,0};copies.push_back(e);copied++;}
   else project[owner[t]].push_back(t);
  }
  for(size_t i=0;i<copies.size();i+=BO_BATCH_MAX){bo_batch b{};b.count=std::min(size_t(BO_BATCH_MAX),copies.size()-i);b.entries=uintptr_t(copies.data()+i);call(BO_BATCH,&b);}
  double projection_begin=now();
  for(auto&[id,ids]:project){auto&w=windows.at(id);auto&s=w.surface;
   auto &resident=texture_cache.acquire(id,s.pixels.size());
   if(!resident.valid || resident.revision!=s.revision){
    DamageRect area{0,0,int(s.width),int(s.height)};
    if(resident.valid && s.pending.w && s.pending.h)area=s.pending;
    // Compact sparse rows once, then scatter them with one ordered DMA batch.
    // Both buffers are reusable; BO_SCRATCH is never part of the displayed scene.
    if(sparse_upload && resident.valid && area.h<=32 && area.w*2<int(s.width) && size_t(area.w)*area.h<=BO_SCRATCH_BYTES/4){
     texture_stage.resize(size_t(area.w)*area.h);texture_scatter.clear();
     for(int row=0;row<area.h;row++){
      std::copy_n(s.pixels.data()+size_t(area.y+row)*s.width+area.x,area.w,texture_stage.data()+size_t(row)*area.w);
      bo_batch_entry e{};e.kind=BO_BATCH_COPY;e.op.copy={BO_SCRATCH,unsigned(row*area.w)*4,BO_TEXTURE,(resident.offset+unsigned(area.y+row)*s.width+unsigned(area.x))*4,unsigned(area.w)*4,0};texture_scatter.push_back(e);
     }
     upload(BO_SCRATCH,0,texture_stage.data(),texture_stage.size());uploaded+=texture_stage.size()*4;scatter_bytes+=texture_stage.size()*4;
     bo_batch batch{};batch.count=texture_scatter.size();batch.entries=uintptr_t(texture_scatter.data());call(BO_BATCH,&batch);
    }else{
     unsigned first=area.y*s.width,count=area.h*s.width;upload(BO_TEXTURE,resident.offset+first,s.pixels.data()+first,count);uploaded+=count*4;
    }
    resident.valid=true;resident.revision=s.revision;s.pending={};
   }
   for(size_t off=0;off<ids.size();off+=128){unsigned count=std::min(size_t(128),ids.size()-off);auto &d=descriptor_batch;for(unsigned i=0;i<count;i++){unsigned t=ids[off+i];auto v=w.plan.at(t).descriptor;v[0]=resident.offset;v[1]=header+slots[t]*256;v[5]=1|(w.color<<8);d[i]=v;}
    unsigned n0=(count+1)/2,n1=count-n0;upload(BO_DESC0,0,d[0].data(),n0*18);if(n1)upload(BO_DESC1,0,d[n0].data(),n1*18);
    bo_run run{};run.bank=bank;run.scene_words=scene_size();run.texture_words=resident.offset+s.pixels.size();run.jobs[0]=n0;run.jobs[1]=n1;run.timeout_ms=30000;call(BO_RUN,&run);jobs[0]+=n0;jobs[1]+=n1;projected+=count;
   }
  }
  double projection_end=now();for(unsigned t:visible_tiles)if(wanted[t])versions[bank][t]=wanted[t];
  next[0]=(next[0]&0xffffffu)|((bank+1)<<24);if(ui_enabled)ui_compose(next,bank);auto &published_metadata=ui_enabled?ui_metadata:next;
  bo_publish p{};p.present.bank=bank;p.present.scene_words=scene_size();p.present.resolution=2;p.present.timeout_ms=30000;std::copy(camera.begin(),camera.end(),p.present.camera);p.metadata=uintptr_t(published_metadata.data());p.words=header;
  double call_start=now();call(BO_PUBLISH,&p);double end=now();if(ui_enabled)display_metadata=ui_metadata;metadata.swap(next);for(unsigned t:touched)presented[t]=wanted[t];profile=p.profile;active=bank;commits++;
  stats.published=true;stats.changed=changed;stats.content_changed=content_changed;stats.owned=owned;stats.projected=projected;stats.copied=copied;
  stats.uploaded=uploaded;stats.scatter=scatter_bytes;stats.evictions=texture_cache.evictions;stats.resident=texture_cache.entries.size();stats.bank=bank;stats.jobs=jobs;
  stats.plan_ms=plan_ms;stats.plan_tiles=plan_tiles;stats.plan_rejected=plan_rejected;stats.plan_fallbacks=plan_fallbacks;
  stats.projection_ms=(projection_end-projection_begin)*1000;stats.render_ms=(end-begin)*1000;stats.publish_ms=(end-call_start)*1000;stats.profile=profile;
  return describe?render_report():J();
 }
 J attach(Window&w,const J&q){
  Surface s;s.init(q.value("width",240u),q.value("height",144u),w.color);auto components=q.at("components");require(components.is_array()&&components.size()<=128,"component list");std::set<int> ids;for(const auto&c:components){int id=c.at("id");require(ids.insert(id).second,"duplicate component id");s.put(c);}s.revision=++serial;w.surface=std::move(s);w.has_content=true;w.plan_ready=false;texture_cache.erase(w.id);return render();
 }
 J snapshot(){const auto &visible_metadata=ui_enabled&&!display_metadata.empty()?display_metadata:metadata;require(mock,"snapshot is a host reference check only");J hashes=J::array();for(unsigned t=0;t<tiles;t++){uint32_t hash=2166136261u;if(active>=0&&visible_metadata[256+t]!=UINT32_MAX){auto*data=reinterpret_cast<const uint8_t*>(mock_memory[BO_SCENE0+active].data()+header)+visible_metadata[256+t];for(unsigned i=0;i<1024;i++)hash=(hash^data[i])*16777619u;}else for(unsigned i=0;i<1024;i++)hash=hash*16777619u;hashes.push_back(hash);}return {{"tile_hashes",hashes}};}
