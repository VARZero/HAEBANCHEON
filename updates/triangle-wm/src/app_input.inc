 // Discrete application input is FIFO; only WM motion is replaceable.
 struct AppPoint {int window=0,x=0,y=0;};
 std::map<int,std::vector<J>> app_input;
 static constexpr size_t app_event_limit=128;
 J drain_app_events(int fd){
  J events=J::array();auto it=app_input.find(fd);
  if(it!=app_input.end()){for(auto&e:it->second)events.push_back(std::move(e));app_input.erase(it);}
  return json_object("events",events,"focused_window",focused,"generation",generation);
 }
 void complete_app_waits(){
  auto now=microseconds_now();
  for(auto&[fd,p]:peers)if(p.event_wait&&(!app_input[fd].empty()||now>=p.event_deadline_us)){
   reply(p,p.wait_id,drain_app_events(fd));p.wait_id=nullptr;p.event_wait=false;
  }
 }
 void app_event(int id,J event){
  auto&r=records.at(id);auto&queue=app_input[r.owner];require(queue.size()<app_event_limit,"application input queue full");
  event["window_id"]=id;queue.push_back(std::move(event));complete_app_waits();
 }
 AppPoint app_point(int x,int y){
  unsigned tile=engine->ui_pick(x,y);if(tile>=tiles)return {};
  int id=engine->planned_owner[tile];if(!id||!records.count(id))return {};
  auto&w=engine->windows.at(id);if(!w.visible||!w.has_content||w.shell_pending||!w.plan_ready)return {};
  auto ray=engine->ui_ray(x,y);const auto&t=w.transform;auto z=dot(ray,t.ax[0]);if(z<=0)return {};
  double u=(dot(ray,t.ax[1])/z/t.tx+.5)*(records.at(id).width-1);
  double v=(.5-dot(ray,t.ax[2])/z/t.ty)*(records.at(id).height-1);
  if(u<0||v<0||u>records.at(id).width-1||v>records.at(id).height-1)return {};
  return {id,int(std::lround(u)),int(std::lround(v))};
 }
 J app_keyboard(const std::string&op,const J&a){
  require(!engine->pointer.down&&!engine->move_marker.active,"finish the pointer gesture before typing");
  require(focused&&records.count(focused),"no focused application");auto&w=engine->windows.at(focused);
  require(w.visible&&w.has_content&&!w.shell_pending,"focused application is not ready");
  J e=J::object();
  if(op=="input_text"){
   auto text=a.at("text").get<std::string>();require(!text.empty()&&text.size()<=1024,"text length 1..1024");
   for(unsigned char c:text)require((c>=32&&c<=126)||c=='\n'||c=='\t',"example font supports ASCII text only");
   e=json_object("type","text","text",text);
  }else{
   auto key=a.at("key").get<std::string>();
   require(key=="Backspace"||key=="Delete"||key=="ArrowLeft"||key=="ArrowRight"||key=="ArrowUp"||key=="ArrowDown"||key=="Home"||key=="End"||key=="Enter"||key=="Tab"||key=="Save"||key=="Interrupt"||key=="EOF"||key=="ClearScreen"||key=="PageUp"||key=="PageDown","unsupported application key");
   e=json_object("type","key","key",key);
  }
  app_event(focused,std::move(e));return json_object("accepted",true,"window_id",focused);
 }
