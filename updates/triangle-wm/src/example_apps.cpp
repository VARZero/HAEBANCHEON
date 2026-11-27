#include "example_apps.hpp"
#include <csignal>
#include <iostream>
#include "desktop_apps.hpp"
static volatile sig_atomic_t finished=0;
static void finish(int){finished=1;}
int main(int argc,char**argv){try{
 signal(SIGPIPE,SIG_IGN);signal(SIGTERM,finish);signal(SIGINT,finish);
 std::string kind=argc>1?argv[1]:"editor",file="/tmp/boayo-note.txt",output,catalog=getenv("BOAYO_APPS")?getenv("BOAYO_APPS"):"/etc/boayo/apps.json";bool once=false;
 for(int i=2;i<argc;i++){std::string a=argv[i];if(a=="--file"&&i+1<argc)file=argv[++i];else if(a=="--apps"&&i+1<argc)catalog=argv[++i];else if(a=="--render"&&i+1<argc)output=argv[++i];else if(a=="--once")once=true;else throw std::runtime_error("usage: triangle-examples editor|settings|dashboard|launcher|control|terminal [--file PATH] [--apps PATH] [--render PPM] [--once]");}
 if(kind=="launcher"||kind=="control"||kind=="terminal"){char executable[4096];auto n=readlink("/proc/self/exe",executable,sizeof(executable)-1);require(n>0&&n<ssize_t(sizeof(executable)-1),"cannot resolve executable");return desktop_apps::run(kind,catalog,std::string(executable,size_t(n)),output,once,finished);}
 if(!output.empty()){example_apps::Model model(kind,file);model.render().ppm(output);std::cout<<"EXAMPLE_RENDERED "<<output<<std::endl;return 0;}
 app_ui::Window window("BoAYO "+kind,360,240);example_apps::Model model(kind,file);
 std::unique_ptr<boayo::Client> control;
 if(kind=="settings"){control=std::make_unique<boayo::Client>("example-settings",boayo::socket_path()+".control");model.pointer=control->call("input_status").at("display_enabled");}
 window.present(model.render());std::cout<<"EXAMPLE_READY "<<boayo::J{{"app",kind},{"window_id",window.id},{"first_frame","B+content"},{"font","ASCII"}}.dump()<<std::endl;
 if(once)return 0;
 auto last_sample=std::chrono::steady_clock::now();
 while(!finished){
  auto events=window.events();for(auto&e:events){if(e.at("window_id")!=window.id)continue;if(e.value("type",std::string())=="closed"){finished=1;break;}model.event(e);}
  if(finished)break;
  if(events.empty()){
   if(kind!="dashboard"||std::chrono::steady_clock::now()-last_sample<std::chrono::seconds(2))continue;
   model.stats.sample();last_sample=std::chrono::steady_clock::now();
  }
  if(model.apply_pointer){control->call("input_visibility",{{"visible",model.pointer}});model.apply_pointer=false;}
  if(model.apply_colors){window.colors(model.background,model.splash);model.applied_background=model.background;model.apply_colors=false;}
  window.present(model.render());
 }
 return 0;
 }catch(const std::exception&e){if(std::string(e.what())=="window not found")return 0;std::cerr<<e.what()<<std::endl;return 1;}}
