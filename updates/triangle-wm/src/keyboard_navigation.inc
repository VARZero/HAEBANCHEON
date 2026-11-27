 struct FocusTarget {std::string id;int x,y,w,h;};
 std::map<int,std::vector<FocusTarget>> focus_targets;
 int nav_window=0;std::string nav_target;pid_t home_pid=-1;
 std::map<std::string,std::pair<uint32_t,J>> last_navigation;
 J navigation_request(const J&a){
  std::string source=a.value("source",std::string());uint32_t sequence=0;
  if(!source.empty()){
   require(source.size()<=96&&a.at("sequence").is_number_integer(),"navigation source/sequence");int64_t n=a.at("sequence").get<int64_t>();require(n>0&&n<=UINT32_MAX,"navigation sequence range");sequence=uint32_t(n);
   auto i=last_navigation.find(source);if(i!=last_navigation.end()&&i->second.first==sequence){auto r=i->second.second;r["duplicate"]=true;return r;}
  }
  J result;try{result=navigation_key(a);}catch(const std::exception&e){result=json_object("accepted",false,"error",e.what());}
  result["handled"]=true;
  if(!source.empty()){if(!last_navigation.count(source)&&last_navigation.size()>=32)last_navigation.erase(last_navigation.begin());last_navigation[source]={sequence,result};}
  return result;
 }
 void reap_home(){if(home_pid>0&&waitpid(home_pid,nullptr,WNOHANG)==home_pid)home_pid=-1;}
 J navigation_status(){return json_object("window_id",nav_window,"target",nav_target,"home_pending",home_pid>0);}
 void select_target(int id,const std::string&target){
  if(nav_window==id&&nav_target==target)return;
  if(nav_window&&records.count(nav_window)&&nav_window!=id)app_event(nav_window,json_object("type","focus","target",""));
  app_event(id,json_object("type","focus","target",target));nav_window=id;nav_target=target;
  int close_window=target=="$close"?id:0;
  if(engine->keyboard_close_window!=close_window){engine->keyboard_close_window=close_window;engine->ui_mask_dirty=true;changed(false,false);}
 }
 J register_focusables(Peer&p,const J&a){
  auto&r=owned(p,a);int id=a.at("window_id");auto&values=a.at("targets");require(values.is_array()&&values.size()<=128,"focus target limit");
  std::vector<FocusTarget> targets;std::set<std::string> ids;
  for(auto&v:values){FocusTarget t{v.at("id").get<std::string>(),integer(v,"x",0,0,r.width-1),integer(v,"y",0,0,r.height-1),integer(v,"w",1,1,r.width),integer(v,"h",1,1,r.height)};
   require(!t.id.empty()&&t.id.size()<=64&&t.id[0]!='$'&&ids.insert(t.id).second,"invalid focus id");require(t.x+t.w<=int(r.width)&&t.y+t.h<=int(r.height),"focus bounds");if(v.value("enabled",true))targets.push_back(std::move(t));}
  std::sort(targets.begin(),targets.end(),[](const auto&a,const auto&b){return std::tie(a.y,a.x,a.id)<std::tie(b.y,b.x,b.id);});focus_targets[id]=std::move(targets);
  bool retained=std::any_of(focus_targets[id].begin(),focus_targets[id].end(),[&](const auto&t){return t.id==nav_target;});
  if(focused==id&&!focus_targets[id].empty()&&(nav_window!=id||(nav_target!="$close"&&!retained)))select_target(id,focus_targets[id].front().id);
  return navigation_status();
 }
 J navigation_key(const J&a){
  auto key=a.at("key").get<std::string>();require(key=="Home"||key=="ArrowLeft"||key=="ArrowRight"||key=="ArrowUp"||key=="ArrowDown"||key=="Enter","unsupported navigation key");
  if(key=="Home"){
   if(capture.owner>=0){cancel_input();changed(false,false);}reap_home();
   for(auto it=engine->order.rbegin();it!=engine->order.rend();++it)if(records.at(*it).title=="BoAYO launcher"){
    int id=*it;auto&w=engine->windows.at(id);w.visible=true;focused=id;auto&order=engine->order;order.erase(std::find(order.begin(),order.end(),id));order.push_back(id);changed();
    if(!focus_targets[id].empty())select_target(id,focus_targets[id].front().id);return json_object("accepted",true,"window_id",id,"opened",false);}
   if(home_pid<=0){
    const char*binary=getenv("TRIANGLE_EXAMPLES_BIN");if(!binary)binary="/usr/bin/triangle-examples";
    posix_spawn_file_actions_t actions;require(posix_spawn_file_actions_init(&actions)==0,"home spawn setup");
    int rc=posix_spawn_file_actions_addopen(&actions,0,"/dev/null",O_RDONLY,0);
    if(!rc)rc=posix_spawn_file_actions_addopen(&actions,1,"/tmp/triangle-home.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0600);
    if(!rc)rc=posix_spawn_file_actions_adddup2(&actions,1,2);
    char*args[]={const_cast<char*>(binary),const_cast<char*>("launcher"),nullptr};
    if(!rc)rc=posix_spawn(&home_pid,binary,&actions,nullptr,args,::environ);posix_spawn_file_actions_destroy(&actions);
    require(rc==0,std::string("home launch: ")+strerror(rc));
   }return json_object("accepted",true,"home_pending",true);
  }
  require(capture.owner<0&&!engine->move_marker.active,"finish the pointer gesture before navigation");require(focused&&records.count(focused),"no focused window");
  auto&w=engine->windows.at(focused);require(w.visible&&w.has_content&&!w.shell_pending,"focused window is loading");
  auto&targets=focus_targets[focused];require(!targets.empty(),"application has no focus targets");
  auto current=std::find_if(targets.begin(),targets.end(),[&](const auto&t){return nav_window==focused&&t.id==nav_target;});
  if(nav_window!=focused||((nav_target!="$close"||!w.controls_enabled)&&current==targets.end())){select_target(focused,targets.front().id);current=targets.begin();}
  if(key=="Enter"){
   if(nav_target=="$close"){int id=focused;erase(id);focus_targets.erase(id);nav_window=0;nav_target.clear();engine->keyboard_close_window=0;return json_object("accepted",true,"closed",id);}
   app_event(focused,json_object("type","click","x",current->x+current->w/2,"y",current->y+current->h/2,"target",current->id,"source","keyboard"));
  }else if(nav_target=="$close"){
   if(key=="ArrowDown")select_target(focused,targets.front().id);
  }else{
   int dx=key=="ArrowRight"?1:key=="ArrowLeft"?-1:0,dy=key=="ArrowDown"?1:key=="ArrowUp"?-1:0;
   int64_t best=INT64_MAX;const FocusTarget*choice=nullptr;
   for(auto&t:targets){int x=2*(t.x-current->x)+t.w-current->w,y=2*(t.y-current->y)+t.h-current->h;int along=x*dx+y*dy,side=std::abs(x*dy-y*dx);if(along<=0)continue;
    int64_t score=int64_t(along)*along+int64_t(side)*side*4;if(score<best){best=score;choice=&t;}}
   if(choice)select_target(focused,choice->id);else if(key=="ArrowUp"&&w.controls_enabled)select_target(focused,"$close");
  }
  auto result=navigation_status();result["accepted"]=true;return result;
 }
