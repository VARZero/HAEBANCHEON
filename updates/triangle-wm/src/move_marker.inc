 // Preserve the existing base-payload capacity; previews share solid color tiles.
 static constexpr unsigned marker_payload=tiles-257;
 static constexpr unsigned marker_cells=1024;
 static void append_copy(std::vector<bo_batch_entry>&out,unsigned from,unsigned to,unsigned offset,unsigned bytes=1024){
  if(!out.empty()){auto&last=out.back();auto&c=last.op.copy;
   if(last.kind==BO_BATCH_COPY&&c.src_region==from&&c.dst_region==to&&c.src_offset+c.bytes==offset&&c.dst_offset+c.bytes==offset){c.bytes+=bytes;return;}}
  bo_batch_entry e{};e.kind=BO_BATCH_COPY;e.op.copy={from,offset,to,offset,bytes,0};out.push_back(e);
 }
 void run_batches(std::vector<bo_batch_entry>&ops){
  for(size_t i=0;i<ops.size();i+=BO_BATCH_MAX){bo_batch b{};b.count=std::min(size_t(BO_BATCH_MAX),ops.size()-i);b.entries=uintptr_t(ops.data()+i);call(BO_BATCH,&b);}
 }
 void marker_begin(int id,int kind,unsigned t,unsigned color){
  require(active>=0&&t<tiles,"marker needs a visible scene");
  move_marker={};move_marker.active=true;move_marker.needs_publish=true;move_marker.window=id;move_marker.kind=kind;move_marker.tile=t;move_marker.color=color;
  ui_dirty=true;ui_mask_dirty=true;
  if(kind==3)ui_layout_dirty=true;
 }
 bool marker_update(unsigned t){
  auto&m=move_marker;if(t>=tiles||t==m.tile)return false;
  bool hidden=m.frozen&&m.occluded[t]&&m.occluded[m.tile];m.tile=t;if(hidden)return false;
  m.needs_publish=true;ui_dirty=true;return true;
 }
 bool marker_resize(Window&target,bool initial=false){
  auto&m=move_marker;require(m.active&&m.kind==3,"resize preview state");auto begin=microseconds_now();
  if(!initial)target.transform=m.resize_transform;
  auto ids=initial?target.footprint:footprint(target);m.resize_transform=target.transform;
  m.target_yaw=target.yaw;m.target_pitch=target.pitch;m.target_width=target.width;m.target_height=target.height;
  m.resize_prepare_us=microseconds_now()-begin;if(ids==m.rim)return false;
  // Keep the complete target footprint. All entries share one solid S payload.
  m.rim=std::move(ids);m.rim_updates++;m.needs_publish=true;ui_dirty=true;return true;
 }
 bool marker_restore_scene=false;
 void marker_cancel(){if(!move_marker.active&&!move_marker.frozen&&!move_marker.committing)return;marker_restore_scene|=move_marker.kind==3&&move_marker.frozen;move_marker.active=false;move_marker.frozen=false;move_marker.committing=false;move_marker.base.clear();ui_dirty=true;ui_mask_dirty=true;if(move_marker.kind==3)ui_layout_dirty=true;}
 void marker_capture(const Words&base){
  auto&m=move_marker;m.base=base;
  std::map<int,unsigned> rank;for(unsigned i=0;i<order.size();i++)rank[order[i]]=i;
  for(unsigned t=0;t<tiles;t++)m.occluded[t]=planned_owner[t]&&rank[planned_owner[t]]>rank[m.window];
  m.frozen=true;
 }
 void marker_inject(Words&out,unsigned bank){
  auto&m=move_marker;
  if(m.kind==3){
   // The captured window is absent from base: shrinking reveals the scene behind
   // it rather than leaving old content behind. Preserve higher-window occlusion.
   std::vector<unsigned> visible;visible.reserve(m.rim.size());
   unsigned source=colors.at(m.color)*1024;
   for(unsigned t:m.rim)if(!m.occluded[t])visible.push_back(t);
   for(unsigned t:m.bank_rims[bank])if(!std::binary_search(visible.begin(),visible.end(),t))out[256+t]=m.base[256+t];
   for(unsigned t:visible)if(!std::binary_search(m.bank_rims[bank].begin(),m.bank_rims[bank].end(),t))out[256+t]=source;
   m.changed_entries=0;auto a=m.last_rim.begin(),b=visible.begin();
   while(a!=m.last_rim.end()||b!=visible.end()){
    if(b==visible.end()||(a!=m.last_rim.end()&&*a<*b)){++m.changed_entries;++a;}
    else if(a==m.last_rim.end()||*b<*a){++m.changed_entries;++b;}
    else{++a;++b;}
   }
   m.drawn=!visible.empty();m.last_payload_ops=0;m.payload_reuses++;m.last_rim=visible;m.bank_rims[bank]=std::move(visible);return;
  }
  if(m.bank_tiles[bank]<tiles)out[256+m.bank_tiles[bank]]=m.base[256+m.bank_tiles[bank]];
  m.last_payload_ops=0;m.drawn=m.tile<tiles&&!m.occluded[m.tile];
  m.changed_entries=(m.last_tile<tiles?1:0)+(m.drawn&&m.tile!=m.last_tile?1:0);
  if(!m.drawn){m.last_tile=~0u;m.bank_tiles[bank]=~0u;return;}
  // The complete selected triangle points directly at the immutable S tile.
  out[256+m.tile]=colors.at(m.color)*1024;m.payload_reuses++;
  m.last_tile=m.tile;m.bank_tiles[bank]=m.tile;
 }
 void marker_sync_other(unsigned current){
  // One-time synchronization only. Both banks then contain an immutable base.
  unsigned other=current^1;std::vector<unsigned> offsets;
  for(unsigned t:visible_tiles)if(planned_version[t]&&versions[other][t]!=planned_version[t])offsets.push_back((header+unsigned(slots[t])*256)*4);
  for(auto [t,slot]:ui_slots)if(move_marker.base[256+t]==(tiles-256+slot)*1024){
   auto&a=ui_stamps[current][slot];auto&b=ui_stamps[other][slot];
   if(a.tile!=b.tile||a.source!=b.source||a.version!=b.version||a.color!=b.color||!ui_same_mask(a.paint,b.paint))offsets.push_back((header+(tiles-256+slot)*256)*4);
  }
  std::sort(offsets.begin(),offsets.end());offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());std::vector<bo_batch_entry> ops;
  for(unsigned off:offsets)append_copy(ops,BO_SCENE0+current,BO_SCENE0+other,off);run_batches(ops);move_marker.freeze_copies+=ops.size();
  for(unsigned t:visible_tiles)if(planned_version[t])versions[other][t]=planned_version[t];
  for(auto [t,slot]:ui_slots)if(move_marker.base[256+t]==(tiles-256+slot)*1024)ui_stamps[other][slot]=ui_stamps[current][slot];
 }
 J marker_report()const{
  const auto&m=move_marker;auto result=json_object("active",m.active,"mode",m.kind==3?"solid-resize":"single-tile","frozen",m.frozen,"committing",m.committing,"window_id",m.window,"kind",m.kind,"tile",m.kind==2&&m.tile<tiles?J(m.tile):J(),"drawn",m.drawn,"splash_index",m.color,"frames",m.frames,"freeze_copy_ops",m.freeze_copies,"payload_builds",m.payload_builds,"payload_reuses",m.payload_reuses,"changed_entries",m.changed_entries,"payload_ops",m.last_payload_ops);
  result["colored_cells_per_tile"]=m.kind==3?1024u:marker_cells;
  if(m.kind==3){result["filled_tiles"]=m.rim;result["visible_filled_tiles"]=m.last_rim;result["footprint_updates"]=m.rim_updates;result["resize_prepare_us"]=m.resize_prepare_us;result["target"]=json_object("azimuth",m.target_yaw,"elevation",m.target_pitch,"width_deg",m.target_width,"height_deg",m.target_height);}
  return result;
 }
 void render_marker(){
  double begin=now();auto&m=move_marker;require(m.active&&m.frozen&&!m.committing,"marker fast path state");
  if(input_checkpoint)input_checkpoint(true);
  stats={};ui_ms=0;ui_layout_us=ui_pick_us=ui_masks_us=ui_payload_us=0;
  ui_rebuilt=ui_incremental=ui_reused=ui_written_cells=ui_restored_cells=ui_copy_bytes=0;
  // Swapping with the displayed header preserves the other bank's old header.
  // After the first use, restore its old marker entry and set the new one only.
  unsigned bank=unsigned(active)^1;auto&next=m.scratch;if(next.empty())next=m.base;
  next[0]=(next[0]&0xffffff)|((bank+1)<<24);marker_inject(next,bank);
  bo_publish p{};p.present.bank=bank;p.present.scene_words=scene_size();p.present.resolution=2;p.present.timeout_ms=30000;std::copy(camera.begin(),camera.end(),p.present.camera);p.metadata=uintptr_t(next.data());p.words=header;
  double submitted=now();call(BO_PUBLISH,&p);double end=now();display_metadata.swap(next);active=bank;commits++;m.frames++;m.needs_publish=false;ui_dirty=false;
  stats.published=true;stats.bank=bank;stats.owned=visible_tiles.size();stats.render_ms=(end-begin)*1000;stats.publish_ms=(end-submitted)*1000;stats.profile=p.profile;profile=p.profile;
 }
