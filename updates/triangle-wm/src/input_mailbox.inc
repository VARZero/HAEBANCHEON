 // Only movement is replaceable. Legacy input and button operations are barriers.
 struct PendingInput {int owner=-1,x=0,y=0;uint64_t ticket=0,accepted_us=0;uint32_t fast_sequence=0;};
 PendingInput pending_input;
 uint64_t input_accepted=0,input_applied=0,input_visible=0,input_coalesced=0;
 uint64_t input_applied_us=0,input_visible_age_us=0,input_priority_publications=0;
 int input_visible_x=640,input_visible_y=360;
 bool input_priority_used=false;
 uint32_t fast_accepted_sequence=0,fast_visible_sequence=0,input_applied_fast_sequence=0;
 int fast_owner=-1,input_applied_fast_owner=-1;
 bool checkpoint_optimized=!(getenv("TRIANGLE_CHECKPOINT_OPT")&&std::string(getenv("TRIANGLE_CHECKPOINT_OPT"))=="0");
 J input_progress()const{
  return json_object("accepted_ticket",input_accepted,"applied_ticket",input_applied,
    "visible_ticket",input_visible,"coalesced_moves",input_coalesced,
    "visible_x",input_visible_x,"visible_y",input_visible_y,
    "last_accept_to_visible_us",input_visible_age_us,"priority_publications",input_priority_publications,
    "fast_accepted_sequence",fast_accepted_sequence,"fast_visible_sequence",fast_visible_sequence);
 }
 void accept_fast_motion(Peer&p,const std::string&line){
  fast_motion::Motion m{};
  if(!p.control||!engine->ui_enabled||!fast_motion::decode(line,m)||
     (capture.owner>=0&&capture.owner!=p.fd)||(pending_input.owner>=0&&pending_input.owner!=p.fd)){
   p.fast_invalid=true;shutdown(p.fd,SHUT_RDWR);return;
  }
  if(p.fast_last_sequence&&int32_t(m.sequence-p.fast_last_sequence)<=0)return;
  if(!p.fast_last_sequence){fast_visible_sequence=0;fast_owner=p.fd;}
  p.fast_last_sequence=m.sequence;fast_accepted_sequence=m.sequence;
  if(pending_input.owner>=0)++input_coalesced;
  pending_input={p.fd,int(m.x),int(m.y),++input_accepted,microseconds_now(),m.sequence};
  last_input_us=pending_input.accepted_us;
 }
 J submit_input(Peer&p,const J&a){
  require(p.control,"input requires control socket");require(engine->ui_enabled,"interactive mode disabled");
  require(capture.owner<0||capture.owner==p.fd,"pointer captured by another input connection");
  require(pending_input.owner<0||pending_input.owner==p.fd,"pointer submission owned by another connection");
  int x=integer(a,"x",engine->pointer.x,0,1279),y=integer(a,"y",engine->pointer.y,0,719);
  if(pending_input.owner>=0)++input_coalesced;
  pending_input={p.fd,x,y,++input_accepted,microseconds_now()};
  last_input_us=pending_input.accepted_us;
  return json_object("accepted_ticket",input_accepted,"visible_ticket",input_visible,"coalesced_moves",input_coalesced);
 }
 void apply_pending_input(){
  if(pending_input.owner<0)return;
  auto q=pending_input;pending_input={};
  auto it=peers.find(q.owner);require(it!=peers.end(),"pending input owner missing");
  uint64_t before=generation;
  if(q.fast_sequence&&(capture.kind==2||capture.kind==3)&&engine->tile_preview&&engine->move_marker.active&&!engine->move_marker.committing){
   engine->pointer.x=q.x;engine->pointer.y=q.y;engine->pointer.visible=true;
   last_input_us=microseconds_now();
   if(update_marker(q.x,q.y)){engine->ui_mask_dirty=true;changed(false,false);}
  }else input(it->second,"input_move",json_object("x",q.x,"y",q.y));
  input_applied=q.ticket;input_applied_us=q.accepted_us;
  input_applied_fast_sequence=q.fast_sequence;input_applied_fast_owner=q.owner;
  // Hidden/no-op motion has no display work of its own. It must not wait for
  // unrelated content, but cannot complete older input that still needs paint.
  if(generation==before&&!engine->ui_dirty)complete_input();
 }
 void complete_input(){
  if(input_visible<input_applied){
   input_visible=input_applied;input_visible_age_us=microseconds_now()-input_applied_us;
   input_visible_x=engine->pointer.x;input_visible_y=engine->pointer.y;
   if(input_applied_fast_sequence&&input_applied_fast_owner==fast_owner)fast_visible_sequence=input_applied_fast_sequence;
  }
  for(auto&[fd,p]:peers)if(p.input_wait&&!p.wait_id.is_null()&&p.wait_generation<=input_visible){
   reply(p,p.wait_id,input_progress());p.wait_id=nullptr;p.input_wait=false;
  }
 }
 // Called with rendering state live: do not accept peers, process app work,
 // dispatch geometry/button operations, close connections or erase windows here.
 void input_checkpoint(bool apply){
  unsigned budget=4;
  for(auto&[fd,p]:peers)if(p.control&&budget){
   if(p.fast_invalid)continue;
   if(checkpoint_optimized&&p.checkpoint_blocked){
    if(!p.out.empty()){auto sent=send(fd,p.out.data(),p.out.size(),MSG_NOSIGNAL|MSG_DONTWAIT);if(sent>0)p.out.erase(0,sent);}continue;
   }
   char buffer[4096];auto n=recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
   if(n>0)p.in.append(buffer,n);
   if(p.in.size()>65536||wire::invalid(p.in))continue; // normal pump handles the protocol failure
   while(budget&&p.wait_id.is_null()){
    if(!wire::ready(p.in))break;unsigned end=wire::json_mode()?wire::frame_size(p.in):8+wire::word(p.in.data()+4);
    std::string line=wire::json_mode()?p.in.substr(0,end-1):p.in.substr(8,end-8),op;J q;
    if(!wire::json_mode()&&fast_motion::recognizable(line)){
     p.in.erase(0,end);accept_fast_motion(p,line);--budget;if(p.fast_invalid)break;continue;
    }
    try{q=wire::decode(line);op=q.at("op").get<std::string>();}catch(...){if(checkpoint_optimized)p.checkpoint_blocked=true;break;}
    if(op!="input_submit"&&op!="input_progress"&&op!="wait_input"){
     if(checkpoint_optimized){p.deferred_request=std::move(q);p.checkpoint_blocked=true;}break;
    }
    p.in.erase(0,end);request(p,line,std::move(q));--budget;
   }
   if(!p.out.empty()){
    auto sent=send(fd,p.out.data(),p.out.size(),MSG_NOSIGNAL|MSG_DONTWAIT);
    if(sent>0)p.out.erase(0,sent); // normal flush handles failure/disconnect
   }
  }
  if(apply)apply_pending_input();
 }
