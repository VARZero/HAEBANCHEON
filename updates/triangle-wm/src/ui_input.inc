 struct Capture {int owner=-1,id=0,kind=0,x=0,y=0;double yaw=0,pitch=0,width=0,height=0;};
 Capture capture;
 uint64_t last_input_us=0;
 void cancel_input(){capture={};engine->pointer.down=false;engine->ui_dirty=true;engine->ui_mask_dirty=true;}
 static std::pair<double,double> angles(V r){return {std::atan2(r[0],-r[2])*180/pi,std::atan2(r[1],std::hypot(r[0],r[2]))*180/pi};}
 J input_status(){
  J controls=J::array();for(auto&c:engine->ui_controls)controls.push_back(json_object("window_id",c.window,"action",c.kind==1?"close":c.kind==2?"move":"resize","x",c.x,"y",c.y,"tile",c.tile,"hit_size",32));
  return json_object("generation",generation,"x",engine->pointer.x,"y",engine->pointer.y,"visible",engine->pointer.visible,"pressed",engine->pointer.down,"captured_window",capture.id,"controls",controls,"layout","split","commit_on_release",true);
 }
 J input(Peer&p,const std::string&op,const J&a){
  require(p.control,"input requires control socket");require(engine->ui_enabled,"interactive mode disabled");
  if(op=="input_status")return input_status();
  require(capture.owner<0||capture.owner==p.fd,"pointer captured by another input connection");
  if(op=="input_cancel"){bool hide=a.value("hide",false);cancel_input();if(hide)engine->pointer.visible=false;changed(false);return json_object("generation",generation,"cancelled",true);}
  require(op=="input_move"||op=="input_button","unknown input operation");
  int x=integer(a,"x",engine->pointer.x,0,1279),y=integer(a,"y",engine->pointer.y,0,719);
  bool pressed=op=="input_button"?a.at("pressed").get<bool>():engine->pointer.down;
  last_input_us=microseconds_now();
  engine->pointer.x=x;engine->pointer.y=y;engine->pointer.visible=true;engine->ui_mask_dirty=true;
  bool applied=false;int action=capture.kind;
  if(op=="input_button"&&pressed!=engine->pointer.down){
   if(pressed){auto hit=engine->ui_hit(x,y);capture.owner=p.fd;capture.x=x;capture.y=y;capture.id=hit.window;capture.kind=hit.kind;
    if(hit.window){auto&w=engine->windows.at(hit.window);capture.yaw=w.yaw;capture.pitch=w.pitch;capture.width=w.width;capture.height=w.height;focused=hit.window;}
   }else if(capture.id&&records.count(capture.id)){
    int id=capture.id;auto &w=engine->windows.at(id);action=capture.kind;
    if(action==1){auto hit=engine->ui_hit(x,y);if(hit.window==id&&hit.kind==1){erase(id);applied=true;}}
    else if(action==2||action==3){
     Window proposed{};proposed.yaw=capture.yaw;proposed.pitch=capture.pitch;proposed.width=capture.width;proposed.height=capture.height;proposed.padding=w.padding;
     auto first=engine->ui_ray(capture.x,capture.y),last=engine->ui_ray(x,y);
     if(action==2){auto from=angles(first),to=angles(last);proposed.yaw=azimuth(capture.yaw+azimuth(to.first-from.first));proposed.pitch=std::clamp(capture.pitch+to.second-from.second,-80.,80.);}
     else {
      auto ax=axes(capture.yaw,capture.pitch);double z0=dot(first,ax[0]),z1=dot(last,ax[0]);
      if(z0>0&&z1>0){
       double tw=std::tan(rad(capture.width/2)),th=std::tan(rad(capture.height/2));
       double dx=dot(last,ax[1])/z1-dot(first,ax[1])/z0,dy=dot(last,ax[2])/z1-dot(first,ax[2])/z0;
       proposed.width=std::clamp(2*std::atan(std::max(.001,tw+dx/2))*180/pi,8.,100.);
       proposed.height=std::clamp(2*std::atan(std::max(.001,th-dy/2))*180/pi,6.,100.);
       auto anchor=unit(ax[0]-ax[1]*tw+ax[2]*th);tw=std::tan(rad(proposed.width/2));th=std::tan(rad(proposed.height/2));
       double q=anchor[1]*std::sqrt(1+tw*tw+th*th)/std::sqrt(1+th*th);
       if(std::abs(q)<=1){double p0=std::asin(q)-std::atan(th);proposed.pitch=p0*180/pi;proposed.yaw=azimuth(angles(anchor).first+std::atan2(tw,std::cos(p0)-th*std::sin(p0))*180/pi);}
       else proposed.pitch=1000;
      }else proposed.pitch=1000;
     }
     if(std::abs(proposed.pitch)<=80){
      w.yaw=proposed.yaw;w.pitch=proposed.pitch;w.width=proposed.width;w.height=proposed.height;w.footprint=engine->footprint(w);w.plan_ready=false;applied=true;
     }
    }
   }
   engine->pointer.down=pressed;if(!pressed)capture={};
  }
  engine->ui_dirty=true;changed(applied);
  return json_object("generation",generation,"applied",applied,"action",action,"captured_window",capture.id);
 }
