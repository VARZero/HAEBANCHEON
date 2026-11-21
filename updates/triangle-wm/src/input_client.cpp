#include "ipc.hpp"
#include <chrono>
#include <iostream>
#include <csignal>
using namespace boayo;
static double ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**argv){try{
 signal(SIGPIPE,SIG_IGN);Client control("pointer-input",socket_path()+".control");
 auto run=[&](const std::string&op,const J&args){double begin=ms();auto r=control.call(op,args);double accepted=ms(),feedback=accepted;if(r.contains("generation")){if(r.value("applied",false)&&r.value("action",0)>=2){control.call("wait_feedback",{{"generation",r.at("generation")}});feedback=ms();control.call("wait_present",{{"generation",r.at("generation")}});}else{control.call("wait_present",{{"generation",r.at("generation")}});feedback=ms();}}return J{{"result",r},{"accept_ms",accepted-begin},{"feedback_ms",feedback-begin},{"present_ms",ms()-begin}};};
 if(argc==2&&std::string(argv[1])=="--bridge"){
  std::cout<<"INPUT_READY"<<std::endl;std::string line;
  while(std::getline(std::cin,line)){
   if(line=="quit"){run("input_cancel",J::object());break;}
   try{auto q=J::parse(line);auto op=q.at("op").get<std::string>();require(op.rfind("input_",0)==0,"input operations only");auto result=run(op,q.value("args",J::object()));result["ok"]=true;std::cout<<"INPUT_REPLY "<<result.dump()<<std::endl;}
   catch(const std::exception&e){std::cout<<"INPUT_REPLY "<<J{{"ok",false},{"error",e.what()}}.dump()<<std::endl;}
  }return 0;
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
