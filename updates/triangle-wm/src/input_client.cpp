#include "ipc.hpp"
#include "fast_motion.hpp"
#include <chrono>
#include <iostream>
#include <csignal>
#include <algorithm>
#include <poll.h>
using namespace boayo;
static double ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**argv){try{
 signal(SIGPIPE,SIG_IGN);Client control("pointer-input",socket_path()+".control");
 auto send_motion=[&](fast_motion::Motion m){auto packet=fast_motion::frame(m);size_t offset=0;
  while(offset<packet.size()){auto n=send(control.sock.fd,packet.data()+offset,packet.size()-offset,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;require(n>0,"fast input send failed");offset+=n;}
 };
 if(argc==2&&std::string(argv[1])=="--ipc-bench"){
  J samples=J::array();for(unsigned i=0;i<22;i++){double begin=ms();control.call("input_progress");double elapsed=ms()-begin;if(i>=2)samples.push_back(elapsed);}
  std::cout<<"IPC_BENCH "<<J{{"n",samples.size()},{"roundtrip_ms",samples}}.dump()<<std::endl;return 0;
 }
 auto run=[&](const std::string&op,const J&args){double begin=ms();auto r=control.call(op,args);double accepted=ms(),feedback=accepted;J done;
  if(r.contains("generation")){if(r.value("applied",false)&&r.value("action",0)>=2){control.call("wait_feedback",{{"generation",r.at("generation")}});feedback=ms();done=control.call("wait_present",{{"generation",r.at("generation")}});}else{done=control.call("wait_present",{{"generation",r.at("generation")}});feedback=ms();}}
  if(r.contains("transform_ticket")&&done.contains("transform")&&done["transform"].value("ticket",uint64_t(0))==r["transform_ticket"].get<uint64_t>()){
   require(done["transform"].value("state",std::string())!="failed",done["transform"].value("error",std::string("transform failed")));r["transform"]=done["transform"];
  }
  return J{{"result",r},{"accept_ms",accepted-begin},{"feedback_ms",feedback-begin},{"present_ms",ms()-begin}};
 };
 if((argc==2||argc==3)&&std::string(argv[1])=="--marker-bench"){
  unsigned sample_count=argc==3?std::stoul(argv[2]):16;require(sample_count>=4&&sample_count<=256,"marker benchmark sample count 4..256");
  auto initial=control.call("input_status");require(!initial.at("controls").empty(),"create an application window first");
  int id=initial["controls"][0]["window_id"];J original;
  auto initial_state=control.call("get_state");for(auto&w:initial_state.at("windows"))if(w["window_id"]==id)original=w;
  run("input_cancel",{{"hide",true}});run("input_visibility",{{"visible",false}});
  for(const std::string kind:{"move","resize"}){
   J handle;auto state=control.call("input_status");for(auto&c:state["controls"])if(c["window_id"]==id&&c["action"]==kind)handle=c;
   require(!handle.is_null(),"benchmark handle not visible");int x=handle["x"],y=handle["y"];
   auto press=run("input_button",{{"x",x},{"y",y},{"pressed",true}});J samples=J::array();auto before=control.call("get_state")["performance"];
   int target_x=x,target_y=y;
   for(unsigned i=0;i<sample_count;i++){
    target_x=std::clamp(x+(i%2?90:-90),0,1279);target_y=std::clamp(y+(i%2?24:-24),0,719);
    auto start=ms();auto accepted=control.call("input_submit",{{"x",target_x},{"y",target_y}});auto received=ms();
    auto completed=control.call("wait_input",{{"ticket",accepted.at("accepted_ticket")}});auto displayed=ms();
    auto perf=control.call("get_state")["performance"];auto preview=control.call("input_status")["preview"];
    samples.push_back(J{{"index",i},{"accept_ms",received-start},{"client_complete_ms",displayed-start},{"accept_to_visible_us",completed["last_accept_to_visible_us"]},{"preview",preview},{"render",perf["last_render"]},{"publications",perf["publications"]}});
   }
   auto before_release=control.call("get_state")["performance"];auto released=run("input_button",{{"x",target_x},{"y",target_y},{"pressed",false}});auto after=control.call("get_state")["performance"];
   std::cout<<"MARKER_BENCH_SAMPLE "<<J{{"action",kind},{"press",press},{"moves",samples},{"before",before},{"before_release",before_release},{"release",released},{"after",after}}.dump()<<std::endl;
  }
  J restore={{"window_id",id}};for(auto key:{"azimuth","elevation","width_deg","height_deg"})restore[key]=original.at(key);
  run("configure_window",restore);run("input_cancel",{{"hide",true}});run("input_visibility",{{"visible",initial.at("display_enabled")}});
  std::cout<<"MARKER_BENCH_DONE"<<std::endl;return 0;
 }
 if(argc==2&&std::string(argv[1])=="--fast-marker-bench"){
  auto initial=control.call("input_status");require(!wire::json_mode()&&initial.value("fast_motion",false),"fast input unavailable");
  run("input_cancel",{{"hide",true}});run("input_visibility",{{"visible",false}});
  J handle;auto handles=control.call("input_status");for(auto&c:handles["controls"])if(c["action"]=="move")handle=c;
  require(!handle.is_null(),"move handle missing");unsigned x=handle["x"],y=handle["y"];
  run("input_button",{{"x",x},{"y",y},{"pressed",true}});
  auto before=control.call("get_state")["performance"]["last_render"];auto start=ms();
  for(unsigned i=1;i<=16;i++)send_motion({i,std::min(1279u,x+i*3),y});
  auto progress=control.call("input_progress");require(progress.at("fast_accepted_sequence")==16,"latest fast coordinate not accepted");
  auto completed=control.call("wait_input",{{"ticket",progress.at("accepted_ticket")}});auto elapsed=ms()-start;
  auto state=control.call("input_status");require(state.at("x")==std::min(1279u,x+48)&&state.at("y")==y&&state["preview"]["active"],"fast endpoint mismatch");
  require(completed.at("fast_visible_sequence")==16&&state["preview"]["colored_cells_per_tile"]==1024,"fast publication mismatch");
  auto after=control.call("get_state")["performance"]["last_render"];
  for(auto field:{"plan_builds_total","run_submissions_total","footprint_calls_total"})require(before[field]==after[field],"fast motion invoked full rendering");
  std::cout<<"FAST_MARKER_SAMPLE "<<J{{"burst",16},{"final_visible_ms",elapsed},{"progress",completed}}.dump()<<std::endl;
  run("input_cancel",{{"hide",true}});run("input_visibility",{{"visible",initial.at("display_enabled")}});
  std::cout<<"FAST_MARKER_BENCH_DONE"<<std::endl;return 0;
 }
 bool fast_bridge=argc==2&&std::string(argv[1])=="--bridge-fast";
 bool async_bridge=fast_bridge||(argc==2&&std::string(argv[1])=="--bridge-async");
 if(argc==2&&(std::string(argv[1])=="--bridge"||async_bridge)){
  if(fast_bridge)require(!wire::json_mode()&&control.call("input_status").value("fast_motion",false),"fast input unavailable");
  std::cout<<"INPUT_READY"<<std::endl;
  auto command=[&](const std::string&line){
   if(line=="quit"){run("input_cancel",J::object());return false;}
   try{auto q=J::parse(line);auto op=q.at("op").get<std::string>();require(op.rfind("input_",0)==0,"input operations only");J result;
    if(async_bridge&&(op=="input_move"||op=="input_progress")){
     double begin=ms();auto accepted=control.call(op=="input_move"?"input_submit":op,q.value("args",J::object()));
     result={{"result",accepted},{"accept_ms",ms()-begin},{"completion_pending",op=="input_move"}};
    }else result=run(op,q.value("args",J::object()));
    result["ok"]=true;std::cout<<"INPUT_REPLY "<<result.dump()<<std::endl;}
   catch(const std::exception&e){std::cout<<"INPUT_REPLY "<<J{{"ok",false},{"error",e.what()}}.dump()<<std::endl;}
   return true;
  };
  if(fast_bridge){
   // Drain already-arrived coordinates before crossing the socket boundary.
   // Ordinary operations flush the newest coordinate first and preserve order.
   std::string buffer;fast_motion::Motion latest{};bool pending=false,done=false;
   auto flush_motion=[&](){if(pending){send_motion(latest);pending=false;}};
   while(!done){
    // Bound collection so continuing input cannot postpone visible feedback.
    unsigned budget=4;
    do{
     char bytes[4096];auto count=read(STDIN_FILENO,bytes,sizeof(bytes));
     if(count<0&&errno==EINTR)continue;
     require(count>=0,"fast UART read failed");if(!count){done=true;break;}
     buffer.append(bytes,count);size_t end;
     while(!done&&(end=buffer.find('\n'))!=std::string::npos){
      auto line=buffer.substr(0,end);buffer.erase(0,end+1);
      if(!line.empty()&&line[0]=='@'){fast_motion::Motion m{};if(fast_motion::serial_line(line,m)){latest=m;pending=true;}}
      else{flush_motion();done=!command(line);}
     }
     require(buffer.size()<=4096,"fast UART line too long");
     pollfd input{STDIN_FILENO,POLLIN,0};
     if(done||!--budget||poll(&input,1,0)<=0||!(input.revents&(POLLIN|POLLHUP)))break;
    }while(true);
    flush_motion();
   }
  }else{std::string line;while(std::getline(std::cin,line))if(!command(line))break;}
  return 0;
 }
 if(argc==2&&(std::string(argv[1])=="--stream-bench"||std::string(argv[1])=="--stream-bench-async")){
  bool async=std::string(argv[1])=="--stream-bench-async";
  run("input_cancel",{{"hide",true}});run("input_visibility",{{"visible",true}});
  for(int round=0;round<3;round++){
   auto start=ms();J samples=J::array();uint64_t ticket=0;
   for(int i=0;i<16;i++){
    J args={{"x",520+i*12},{"y",350+(i%3)*10}};auto begin=ms();
    if(async){auto r=control.call("input_submit",args);ticket=r.at("accepted_ticket");samples.push_back(J{{"index",i},{"accept_ms",ms()-begin},{"ticket",ticket}});}
    else {auto r=run("input_move",args);r["index"]=i;samples.push_back(r);}
   }
   auto accepted=ms();J progress=async?control.call("wait_input",{{"ticket",ticket}}):J::object();auto done=ms();
   auto state=control.call("input_status");require(state.at("x")==700&&state.at("y")==350,"stream endpoint mismatch");
   std::cout<<"STREAM_SAMPLE "<<J{{"async",async},{"round",round},{"requests",16},{"last_accept_ms",accepted-start},{"final_visible_ms",done-start},{"progress",progress},{"samples",samples}}.dump()<<std::endl;
  }
  run("input_cancel",{{"hide",true}});std::cout<<"STREAM_BENCH_DONE"<<std::endl;return 0;
 }
 auto sample=[&](const std::string&name,const std::string&op,const J&a){auto r=run(op,a);r["name"]=name;std::cout<<"INPUT_SAMPLE "<<r.dump()<<std::endl;return r;};
 auto state=control.call("input_status");require(!state.at("controls").empty(),"create an application window first");int id=state.at("controls")[0].at("window_id");
 auto button=[&](const std::string&kind){auto s=control.call("input_status");for(auto&c:s.at("controls"))if(c.at("window_id")==id&&c.at("action")==kind)return c;throw std::runtime_error("button not visible: "+kind);};
 for(int i=0;i<9;i++)sample("pointer_move","input_move",{{"x",540+i*20},{"y",350+(i%3)*10}});
 auto c=button("close");sample("close_press","input_button",{{"x",c["x"]},{"y",c["y"]},{"pressed",true}});sample("close_cancel","input_button",{{"x",int(c["x"])+80},{"y",c["y"]},{"pressed",false}});
 for(int i=0;i<4;i++){c=button("move");int x=c["x"],y=c["y"];sample("move_press","input_button",{{"x",x},{"y",y},{"pressed",true}});sample("drag_pointer","input_move",{{"x",x+(i%2?-32:32)},{"y",y}});sample("move_release","input_button",{{"x",x+(i%2?-32:32)},{"y",y},{"pressed",false}});}
 for(int i=0;i<4;i++){c=button("resize");int x=c["x"],y=c["y"];sample("resize_press","input_button",{{"x",x},{"y",y},{"pressed",true}});sample("resize_preview","input_move",{{"x",x+(i%2?-28:28)},{"y",y+(i%2?-18:18)}});sample("resize_release","input_button",{{"x",x+(i%2?-28:28)},{"y",y+(i%2?-18:18)},{"pressed",false}});}
 for(int i=0;i<5;i++)sample("view_pose","view_set",{{"yaw",i==4?0:i%2?4:-4},{"pitch",0}});
 if(argc==2&&std::string(argv[1])=="--close"){c=button("close");sample("close_press","input_button",{{"x",c["x"]},{"y",c["y"]},{"pressed",true}});sample("close_release","input_button",{{"x",c["x"]},{"y",c["y"]},{"pressed",false}});}
 sample("pointer_hide","input_cancel",{{"hide",true}});std::cout<<"INPUT_BENCH_DONE"<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
