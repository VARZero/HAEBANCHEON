 struct TransformRollback {
  int id;double yaw,pitch,width,height;bool plan_ready,shell_pending,splash_pending;
  WindowTransform transform;std::vector<unsigned> footprint;TilePlans plan;
  std::array<int,tiles> slots;std::vector<unsigned> free_slots,visible;unsigned next_slot;
 };
 std::unique_ptr<TransformRollback> transform_rollback;
 struct PendingTransform {int id;Window geometry;};
 std::unique_ptr<PendingTransform> pending_transform;
 uint64_t transform_ticket=0;std::string transform_phase="idle",transform_error;
 void queue_transform(int id,Window proposed){Engine::validate(proposed);pending_transform=std::make_unique<PendingTransform>(PendingTransform{id,std::move(proposed)});transform_phase="queued";engine->move_marker.committing=true;}
 J transform_status()const{return json_object("ticket",transform_ticket,"state",transform_phase,"error",transform_error);}
 J presentation_result(uint64_t ready,bool early,bool preview=false){auto j=json_object("generation",ready,"presented",!early,"feedback",early,"preview_only",preview,"content_generation",presented);if(transform_ticket)j["transform"]=transform_status();return j;}
 void stage_transform(int id,Window proposed){
  Engine::validate(proposed);auto&w=engine->windows.at(id);
  proposed.id=id;proposed.color=w.color;proposed.splash_color=w.splash_color;proposed.has_content=w.has_content;
  proposed.surface.width=w.surface.width;proposed.surface.height=w.surface.height;
  proposed.transform=w.transform;
  proposed.footprint=engine->footprint(proposed);engine->prepare(proposed);
  // Preflight exact visible payload demand before mutating live geometry.
  std::array<int,tiles> owners{};
  for(int other:engine->order){auto&v=other==id?proposed:engine->windows.at(other);if(v.visible)for(unsigned t:v.footprint)owners[t]=other;}
  unsigned required=engine->colors.size();
  for(unsigned t=0;t<tiles;t++)if(owners[t]){auto&v=owners[t]==id?proposed:engine->windows.at(owners[t]);if(v.has_content&&(!v.plan_ready||v.plan.find(t)!=v.plan.end()))++required;}
  require(required<=Engine::marker_payload,"scene payload capacity");
  auto backup=std::make_unique<TransformRollback>();backup->id=id;backup->yaw=w.yaw;backup->pitch=w.pitch;backup->width=w.width;backup->height=w.height;
  backup->plan_ready=w.plan_ready;backup->shell_pending=w.shell_pending;backup->splash_pending=w.splash_pending;backup->transform=w.transform;
  backup->slots=engine->slots;backup->free_slots=engine->free_slots;backup->visible=engine->visible_tiles;backup->next_slot=engine->next_slot;
  backup->footprint=std::move(w.footprint);backup->plan=std::move(w.plan);
  w.yaw=proposed.yaw;w.pitch=proposed.pitch;w.width=proposed.width;w.height=proposed.height;w.transform=proposed.transform;
  w.footprint=std::move(proposed.footprint);w.plan=std::move(proposed.plan);w.plan_ready=proposed.plan_ready;w.shell_pending=false;w.splash_pending=false;
  transform_rollback=std::move(backup);transform_phase="pending";engine->move_marker.committing=true;
 }
 void finish_transform(){
  if(!transform_rollback)return;transform_rollback.reset();transform_phase="complete";
  auto&m=engine->move_marker;m.active=false;m.frozen=false;m.committing=false;m.base.clear();
 }
 void prepare_transform(){
  if(!pending_transform)return;auto p=std::move(pending_transform);
  try{if(engine->mock&&getenv("TRIANGLE_MOCK_FAIL_TRANSFORM")&&std::string(getenv("TRIANGLE_MOCK_FAIL_TRANSFORM"))=="preflight")throw std::runtime_error("injected transform preflight failure");stage_transform(p->id,std::move(p->geometry));}
  catch(const std::exception&e){transform_phase="failed";transform_error=e.what();engine->marker_cancel();}
 }
 void rollback_transform(const std::string&error){
  auto&b=*transform_rollback;auto&w=engine->windows.at(b.id);
  w.yaw=b.yaw;w.pitch=b.pitch;w.width=b.width;w.height=b.height;w.transform=b.transform;w.footprint=std::move(b.footprint);w.plan=std::move(b.plan);w.plan_ready=b.plan_ready;
  w.shell_pending=b.shell_pending;w.splash_pending=b.splash_pending;engine->slots=b.slots;engine->free_slots=std::move(b.free_slots);engine->visible_tiles=std::move(b.visible);engine->next_slot=b.next_slot;
  for(auto&v:engine->versions)v.fill(0);engine->presented.fill(0);engine->touched_flag.fill(0);engine->ui_stamps={};engine->ui_slots.clear();
  engine->ui_layout_dirty=true;engine->ui_mask_dirty=true;engine->marker_cancel();
  transform_rollback.reset();transform_phase="failed";transform_error=error;scene_dirty=true;dirty=true;
 }
