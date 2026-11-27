 struct Capture {int owner=-1,id=0,kind=0,x=0,y=0,preview_x=INT_MIN,preview_y=INT_MIN;double yaw=0,pitch=0,width=0,height=0;V press_ray{},center_plane{},target_ray{};
  bool resize_cached=false;std::array<V,3> resize_axes{};V resize_anchor{};double resize_tw=0,resize_th=0,resize_z0=0,resize_x0=0,resize_y0=0;
 };
 Capture capture;
 Window proposal(int x,int y){
     Window proposed{};proposed.yaw=capture.yaw;proposed.pitch=capture.pitch;proposed.width=capture.width;proposed.height=capture.height;proposed.padding=engine->windows.at(capture.id).padding;
     auto first=engine->ui_ray(capture.x,capture.y),last=engine->ui_ray(x,y);
     if(capture.kind==2){
      if(engine->tile_preview){auto target=angles(capture.target_ray);proposed.yaw=azimuth(target.first);proposed.pitch=std::clamp(target.second,-80.,80.);}
      else {auto from=angles(first),to=angles(last);proposed.yaw=azimuth(capture.yaw+azimuth(to.first-from.first));proposed.pitch=std::clamp(capture.pitch+to.second-from.second,-80.,80.);}
     }
     else {
      if(!capture.resize_cached){
       capture.resize_axes=axes(capture.yaw,capture.pitch);
       capture.resize_tw=std::tan(rad(capture.width/2));capture.resize_th=std::tan(rad(capture.height/2));
       capture.resize_z0=dot(first,capture.resize_axes[0]);
       if(capture.resize_z0>0){capture.resize_x0=dot(first,capture.resize_axes[1])/capture.resize_z0;capture.resize_y0=dot(first,capture.resize_axes[2])/capture.resize_z0;}
       capture.resize_anchor=unit(capture.resize_axes[0]-capture.resize_axes[1]*capture.resize_tw+capture.resize_axes[2]*capture.resize_th);
       capture.resize_cached=true;
      }
      const auto&ax=capture.resize_axes;double z0=capture.resize_z0,z1=dot(last,ax[0]);
      if(z0>0&&z1>0){
       double tw=capture.resize_tw,th=capture.resize_th;
       double dx=dot(last,ax[1])/z1-capture.resize_x0,dy=dot(last,ax[2])/z1-capture.resize_y0;
       proposed.width=std::clamp(2*std::atan(std::max(.001,tw+dx/2))*180/pi,8.,100.);
       proposed.height=std::clamp(2*std::atan(std::max(.001,th-dy/2))*180/pi,6.,100.);
       const auto&anchor=capture.resize_anchor;tw=std::tan(rad(proposed.width/2));th=std::tan(rad(proposed.height/2));
       double q=anchor[1]*std::sqrt(1+tw*tw+th*th)/std::sqrt(1+th*th);
       if(std::abs(q)<=1){double p0=std::asin(q)-std::atan(th);proposed.pitch=p0*180/pi;proposed.yaw=azimuth(angles(anchor).first+std::atan2(tw,std::cos(p0)-th*std::sin(p0))*180/pi);}
       else proposed.pitch=1000;
      }else proposed.pitch=1000;
     }
  return proposed;
 }
 uint64_t last_input_us=0;
 void cancel_input(){engine->ui_clear_preview();engine->marker_cancel();capture={};engine->pointer.down=false;engine->ui_dirty=true;engine->ui_mask_dirty=true;}
 bool update_marker(int x,int y){
  if(capture.kind==2){capture.target_ray=capture.center_plane+(engine->ui_ray(x,y)-capture.press_ray);return engine->marker_update(engine->ui_pick_ray(capture.target_ray));}
  if(x==capture.preview_x&&y==capture.preview_y)return false;
  capture.preview_x=x;capture.preview_y=y;auto proposed=proposal(x,y);
  if(std::abs(proposed.pitch)>80)return false;
  return engine->marker_resize(proposed);
 }
 static std::pair<double,double> angles(V r){return {std::atan2(r[0],-r[2])*180/pi,std::atan2(r[1],std::hypot(r[0],r[2]))*180/pi};}
 J input_status(){
  J controls=J::array();for(auto&c:engine->ui_controls)controls.push_back(json_object("window_id",c.window,"action",c.kind==1?"close":c.kind==2?"move":"resize","x",c.x,"y",c.y,"tile",c.tile,"hit_size",wm_controls::hit_size,"icon_size",wm_controls::icon_size));
  auto preview=json_object("active",engine->preview.window!=0,"window_id",engine->preview.window,"outline_tiles",engine->preview.masks.size(),"splash_index",engine->preview.color,"mode","outline");
  if(engine->tile_preview){preview=engine->marker_report();preview["outline_tiles"]=0;}
  J colors=J::array();for(int id:engine->order){const auto&w=engine->windows.at(id);colors.push_back(json_object("window_id",id,"background_index",w.color,"splash_index",w.splash_color));}
  return json_object("generation",generation,"x",engine->pointer.x,"y",engine->pointer.y,"visible",engine->pointer.visible&&engine->pointer.display,"display_enabled",engine->pointer.display,"pointer_drawn",engine->pointer.visible&&engine->pointer.display&&(!engine->move_marker.active||engine->move_marker.committing),"pressed",engine->pointer.down,"captured_window",capture.id,"controls",controls,"window_colors",colors,"layout","split","commit_on_release",true,"fast_motion",!wire::json_mode(),"preview",preview,"transform",transform_status());
 }
 J input(Peer&p,const std::string&op,const J&a){
  require(p.control,"input requires control socket");require(engine->ui_enabled,"interactive mode disabled");
  if(op=="input_status")return input_status();
  if(op=="input_nav")return navigation_request(a);
  require(capture.owner<0||capture.owner==p.fd,"pointer captured by another input connection");
  if(op=="input_text"||op=="input_key")return app_keyboard(op,a);
  if(op=="input_colors"){
   require(!engine->pointer.down,"finish or cancel the drag before changing colors");
   int id=integer(a,"window_id",focused,1,INT_MAX);require(records.count(id),"unknown window");
   J update=a;update["window_id"]=id;return dispatch(p,"configure_window",update);
  }
  if(op=="input_visibility"){bool visible=a.at("visible").get<bool>();if(engine->pointer.display!=visible){engine->pointer.display=visible;engine->ui_mask_dirty=true;changed(false,false);}return json_object("generation",generation,"display_enabled",visible);}
  if(op=="input_cancel"){bool hide=a.value("hide",false);cancel_input();if(hide)engine->pointer.visible=false;changed(false,false);return json_object("generation",generation,"cancelled",true);}
  require(op=="input_move"||op=="input_button","unknown input operation");
  int x=integer(a,"x",engine->pointer.x,0,1279),y=integer(a,"y",engine->pointer.y,0,719);
  bool pressed=op=="input_button"?a.at("pressed").get<bool>():engine->pointer.down;
  const auto previous=engine->pointer;
  last_input_us=microseconds_now();
  engine->pointer.x=x;engine->pointer.y=y;engine->pointer.visible=true;
  bool applied=false;bool had_preview=engine->preview.window!=0;int action=capture.kind;
  bool marker_changed=false;bool had_marker=engine->move_marker.active;uint64_t ticket=0;
  if(engine->tile_preview&&engine->pointer.down&&capture.id&&(capture.kind==2||capture.kind==3))marker_changed=update_marker(x,y);
  if(op=="input_button"&&pressed!=engine->pointer.down){
   if(pressed){auto hit=engine->ui_hit(x,y);auto target=hit.window?AppPoint{}:app_point(x,y);
    if(target.window){require(app_input[records.at(target.window).owner].size()<app_event_limit,"application input queue full");hit.window=target.window;hit.kind=4;}
    capture.owner=p.fd;capture.x=x;capture.y=y;capture.id=hit.window;capture.kind=hit.kind;
    if(hit.window){auto&w=engine->windows.at(hit.window);capture.yaw=w.yaw;capture.pitch=w.pitch;capture.width=w.width;capture.height=w.height;focused=hit.window;
     if(engine->tile_preview&&(hit.kind==2||hit.kind==3)){
      capture.press_ray=engine->ui_ray(x,y);auto center=w.transform.ready?w.transform.ax[0]:axes(w.yaw,w.pitch)[0];auto depth=dot(center,engine->view_axes[0]);
      if(depth<=0){capture={};engine->pointer=previous;throw std::runtime_error("window center behind view");}capture.center_plane=center*(1/depth);capture.target_ray=capture.center_plane;
      unsigned tile=hit.kind==2?engine->ui_pick_ray(center):engine->ui_pick(x,y);engine->marker_begin(hit.window,hit.kind,tile,w.splash_color);marker_changed=true;
      if(hit.kind==3){capture.preview_x=x;capture.preview_y=y;engine->marker_resize(w,true);}
     }
    }
   }else if(capture.id&&records.count(capture.id)){
    int id=capture.id;auto &w=engine->windows.at(id);action=capture.kind;
    if(action==1){auto hit=engine->ui_hit(x,y);if(hit.window==id&&hit.kind==1){erase(id);applied=true;}}
    else if(action==4){auto target=app_point(x,y);if(target.window==id)app_event(id,json_object("type","click","x",target.x,"y",target.y));}
    else if(action==2||action==3){
     Window proposed=proposal(x,y);
     if(engine->tile_preview&&std::abs(proposed.pitch)>80){ticket=++transform_ticket;transform_phase="failed";transform_error="target transform outside view bounds";}
     if(std::abs(proposed.pitch)<=80&&(std::abs(w.yaw-proposed.yaw)>1e-8||std::abs(w.pitch-proposed.pitch)>1e-8||std::abs(w.width-proposed.width)>1e-8||std::abs(w.height-proposed.height)>1e-8)){
      if(engine->tile_preview){
       ticket=++transform_ticket;transform_error.clear();
       try{queue_transform(id,std::move(proposed));applied=true;}catch(const std::exception&e){transform_phase="failed";transform_error=e.what();engine->marker_cancel();}
      }else {w.yaw=proposed.yaw;w.pitch=proposed.pitch;w.width=proposed.width;w.height=proposed.height;w.footprint=engine->footprint(w);w.plan_ready=false;w.shell_pending=true;w.splash_pending=false;applied=true;}
     }
    }
   }
   engine->pointer.down=pressed;if(!pressed){engine->ui_clear_preview();if(!transform_rollback&&!pending_transform)engine->marker_cancel();capture={};}
  }
  bool preview_changed=false;
  if(engine->pointer.down&&capture.id&&(capture.kind==2||capture.kind==3)){
   if(engine->tile_preview)preview_changed=marker_changed;
   else {auto proposed=proposal(x,y);if(std::abs(proposed.pitch)<=80)preview_changed=engine->ui_preview(proposed,capture.id,engine->windows.at(capture.id).splash_color);}
  }
  bool marker_active=engine->tile_preview&&engine->move_marker.active&&!engine->move_marker.committing;
  if(applied||preview_changed||marker_changed||ticket||(had_marker&&!engine->move_marker.active)||(had_preview&&!engine->preview.window)||(!marker_active&&engine->ui_pointer_changed(previous))){engine->ui_mask_dirty=true;changed(applied,applied);}
  auto result=json_object("generation",generation,"applied",applied,"action",action,"captured_window",capture.id);
  if(ticket){result["transform_ticket"]=ticket;result["commit_pending"]=bool(transform_rollback||pending_transform);if(!transform_error.empty())result["transform_error"]=transform_error;}
  return result;
 }
