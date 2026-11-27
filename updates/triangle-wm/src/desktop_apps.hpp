#pragma once
#include "app_ui.hpp"
#include <array>
#include <csignal>
#include <deque>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <iostream>

namespace desktop_apps {
using app_ui::Canvas;
struct Entry {std::string id,title;std::vector<std::string> command;std::array<uint16_t,16> icon{};unsigned color=255;};
struct Catalog {
 std::vector<Entry> entries;
 static Catalog load(const std::string&path){
  std::ifstream f(path,std::ios::binary);require(bool(f),"cannot open app catalog");std::string bytes(65537,'\0');f.read(bytes.data(),bytes.size());bytes.resize(size_t(f.gcount()));require(bytes.size()<=65536,"app catalog exceeds 64 KiB");
  auto root=J::parse(bytes);require(root.at("version")==1&&root.at("apps").is_array()&&root.at("apps").size()<=24,"catalog version / app limit");Catalog result;std::set<std::string> ids;
  for(auto&v:root.at("apps")){
   Entry e;e.id=v.at("id").get<std::string>();e.title=v.at("title").get<std::string>();
   require(!e.id.empty()&&e.id.size()<=32&&ids.insert(e.id).second,"invalid or duplicate app id");
   require(!e.title.empty()&&e.title.size()<=14&&app_ui::ascii(e.title)&&e.title.find_first_of("\n\t")==std::string::npos,"app title must be 1..14 ASCII characters");
   auto&cmd=v.at("command");require(cmd.is_array()&&!cmd.empty()&&cmd.size()<=32,"command must be an argument array");size_t total=0;
   for(auto&a:cmd){auto s=a.get<std::string>();total+=s.size();require(s.size()<=1024&&s.find('\0')==std::string::npos&&total<=8192,"command argument limit");e.command.push_back(std::move(s));}
   require(e.command[0]=="@examples"||(!e.command[0].empty()&&e.command[0][0]=='/'),"use @examples or an absolute executable path");
   int color=v.value("color",255);require(color>=0&&color<=255,"icon color 0..255");e.color=unsigned(color);
   auto&icon=v.at("icon16");require(icon.is_array()&&icon.size()==16,"icon16 requires 16 rows");
   for(unsigned y=0;y<16;y++){auto row=icon.at(y).get<std::string>();require(row.size()==4&&row.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos,"icon row must be four hex digits");e.icon[y]=uint16_t(std::stoul(row,nullptr,16));}
   result.entries.push_back(std::move(e));
  }return result;
 }
};
inline void icon(Canvas&c,const Entry&e,int x,int y){for(unsigned row=0;row<16;row++)for(unsigned col=0;col<16;col++)if(e.icon[row]&(1u<<(15-col)))c.rect(x+int(col)*3,y+int(row)*3,3,3,e.color);}
struct Children {
 std::vector<pid_t> pids;
 void reap(){for(auto i=pids.begin();i!=pids.end();)if(waitpid(*i,nullptr,WNOHANG)!=0)i=pids.erase(i);else ++i;}
 pid_t start(std::vector<std::string> args,const std::string&self,bool detached=true){
  require(!args.empty(),"empty command");if(args[0]=="@examples")args[0]=self;
  std::vector<char*> av;for(auto&s:args)av.push_back(s.data());av.push_back(nullptr);
  int error_pipe[2];require(pipe2(error_pipe,O_CLOEXEC)==0,"launch error pipe");pid_t pid=fork();
  if(pid<0){close(error_pipe[0]);close(error_pipe[1]);throw std::runtime_error("cannot fork application");}
  if(pid==0){
   close(error_pipe[0]);if(detached)setsid();
   int input=open("/dev/null",O_RDONLY),log=open("/tmp/triangle-launcher-apps.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0600);
   if(input>=0)dup2(input,0);if(log>=0){dup2(log,1);dup2(log,2);}if(input>2)close(input);if(log>2)close(log);
   execv(av[0],av.data());int error=errno;ssize_t written;do{written=write(error_pipe[1],&error,sizeof(error));}while(written<0&&errno==EINTR);_exit(127);
  }
  close(error_pipe[1]);int error=0;ssize_t n;do{n=read(error_pipe[0],&error,sizeof(error));}while(n<0&&errno==EINTR);close(error_pipe[0]);
  if(n!=0){waitpid(pid,nullptr,0);throw std::runtime_error(std::string("launch failed: ")+strerror(error?error:EIO));}pids.push_back(pid);return pid;
 }
};

// Fixed-size VT screen. Output parsing has bounded state even for malformed escapes.
struct TerminalScreen {
 static constexpr int columns=54,rows=18;
 struct Cell {char ch=' ';uint8_t fg=255,bg=0;bool operator==(const Cell&o)const{return ch==o.ch&&fg==o.fg&&bg==o.bg;}};
 using Line=std::array<Cell,columns>;
 std::array<Line,rows> cells{};std::deque<Line> history;
 int x=0,y=0,saved_x=0,saved_y=0,top=0,bottom=rows-1,scroll=0;uint8_t fg=255,bg=0;bool cursor=true,wrap=true,pending_wrap=false,alternate=false;
 std::array<Line,rows> primary{};int primary_x=0,primary_y=0;bool primary_wrap=false;
 enum {normal,escape,csi,osc,osc_escape,charset} state=normal;std::string params;size_t ignored=0;
 Cell blank()const{return {' ',fg,bg};}
 void clear_line(Line&l){l.fill(blank());}
 void scroll_up(){if(top==0&&bottom==rows-1&&!alternate){history.push_back(cells[0]);if(history.size()>200)history.pop_front();if(scroll)scroll=std::min(int(history.size()),scroll+1);}for(int r=top;r<bottom;r++)cells[r]=cells[r+1];clear_line(cells[bottom]);}
 void newline(){pending_wrap=false;if(y==bottom)scroll_up();else y=std::min(rows-1,y+1);}
 void reverse(){if(y>top)y--;else{for(int r=bottom;r>top;r--)cells[r]=cells[r-1];clear_line(cells[top]);}}
 void reset(){*this=TerminalScreen{};}
 std::vector<int> numbers()const{std::vector<int> out;int n=0;for(char c:params){if(c>='0'&&c<='9')n=std::min(9999,n*10+c-'0');else if(c==';'){out.push_back(n);n=0;}}out.push_back(n);return out;}
 void dispatch(char command){
  auto p=numbers();auto at=[&](size_t i,int fallback){return i<p.size()&&p[i]?p[i]:fallback;};int n=at(0,1);bool priv=!params.empty()&&params[0]=='?';pending_wrap=false;
  if(priv&&(command=='h'||command=='l')){for(int mode:p){bool on=command=='h';if(mode==25)cursor=on;else if(mode==7)wrap=on;else if(mode==1049&&on!=alternate){if(on){primary=cells;primary_x=x;primary_y=y;primary_wrap=wrap;for(auto&l:cells)clear_line(l);x=y=0;}else{cells=primary;x=primary_x;y=primary_y;wrap=primary_wrap;}alternate=on;scroll=0;top=0;bottom=rows-1;}}return;}
  if(command=='A')y=std::max(0,y-n);else if(command=='B')y=std::min(rows-1,y+n);else if(command=='C')x=std::min(columns-1,x+n);else if(command=='D')x=std::max(0,x-n);
  else if(command=='G')x=std::clamp(n-1,0,columns-1);else if(command=='d')y=std::clamp(n-1,0,rows-1);
  else if(command=='H'||command=='f'){y=std::clamp(at(0,1)-1,0,rows-1);x=std::clamp(at(1,1)-1,0,columns-1);}
  else if(command=='J'){int mode=p[0];if(mode==2||mode==3){for(auto&l:cells)clear_line(l);if(mode==3){history.clear();scroll=0;}}else if(mode==0){for(int i=x;i<columns;i++)cells[y][i]=blank();for(int r=y+1;r<rows;r++)clear_line(cells[r]);}else if(mode==1){for(int r=0;r<y;r++)clear_line(cells[r]);for(int i=0;i<=x;i++)cells[y][i]=blank();}}
  else if(command=='K'){int mode=p[0];for(int i=0;i<columns;i++)if(mode==2||(mode==0&&i>=x)||(mode==1&&i<=x))cells[y][i]=blank();}
  else if(command=='m'){
   const unsigned colors[8]={0,228,3,227,28,252,31,255};
   for(size_t i=0;i<p.size();i++){int code=p[i];if(code==0){fg=255;bg=0;}else if(code==7)std::swap(fg,bg);else if(code==39)fg=255;else if(code==49)bg=0;else if(code>=30&&code<=37)fg=colors[code-30];else if(code>=40&&code<=47)bg=colors[code-40];else if(code>=90&&code<=97)fg=colors[code-90];else if(code>=100&&code<=107)bg=colors[code-100];else if((code==38||code==48)&&i+1<p.size()){if(p[i+1]==5)i=std::min(p.size()-1,i+2);else if(p[i+1]==2)i=std::min(p.size()-1,i+4);}}
  }
  else if(command=='s'){saved_x=x;saved_y=y;}else if(command=='u'){x=saved_x;y=saved_y;}
  else if(command=='r'){int a=at(0,1)-1,b=at(1,rows)-1;if(a>=0&&b<rows&&a<b){top=a;bottom=b;x=y=0;}}
  else if(command=='P'){n=std::min(n,columns-x);for(int i=x;i<columns-n;i++)cells[y][i]=cells[y][i+n];for(int i=columns-n;i<columns;i++)cells[y][i]=blank();}
  else if(command=='@'){n=std::min(n,columns-x);for(int i=columns-1;i>=x+n;i--)cells[y][i]=cells[y][i-n];for(int i=x;i<x+n;i++)cells[y][i]=blank();}
 }
 void feed(const std::string&bytes){for(unsigned char c:bytes){
  if(state==osc||state==osc_escape){if(c==7||(state==osc_escape&&c=='\\')){state=normal;ignored=0;}else{state=c==27?osc_escape:osc;if(++ignored>4096){state=normal;ignored=0;}}continue;}
  if(c==27){state=escape;params.clear();continue;}
  if(state==charset){state=normal;continue;}
  if(state==escape){state=normal;if(c=='['){state=csi;params.clear();}else if(c==']'){state=osc;ignored=0;}else if(c=='('||c==')')state=charset;else if(c=='7'){saved_x=x;saved_y=y;}else if(c=='8'){x=saved_x;y=saved_y;pending_wrap=false;}else if(c=='D')newline();else if(c=='M')reverse();else if(c=='E'){x=0;newline();}else if(c=='c')reset();continue;}
  if(state==csi){if(c>=0x40&&c<=0x7e){dispatch(char(c));state=normal;}else if(params.size()<64&&c>=0x20&&c<=0x3f)params+=char(c);else{state=normal;params.clear();}continue;}
  if(c=='\r'){x=0;pending_wrap=false;}else if(c=='\n'||c=='\v'||c=='\f')newline();else if(c=='\b'){x=std::max(0,x-1);pending_wrap=false;}else if(c=='\t'){x=std::min(columns-1,(x/8+1)*8);pending_wrap=false;}
  else if(c>=32&&c<=126){if(pending_wrap){if(wrap){x=0;newline();}pending_wrap=false;}cells[y][x]={char(c),fg,bg};if(x<columns-1)x++;else pending_wrap=wrap;}
 }}
 std::string text()const{std::string s;for(auto&l:cells){for(auto&c:l)s+=c.ch;s+='\n';}return s;}
 const Line&visible(int row)const{int index=int(history.size())+row-scroll;return index<int(history.size())?history[size_t(std::max(0,index))]:cells[size_t(index-int(history.size()))];}
 Canvas render(const std::string&status)const{
  Canvas c;c.clear(0);c.target("interrupt",228,12,52,25);c.target("escape",290,12,54,25);c.target("terminal",16,49,328,165);c.text(16,17,"Terminal",255,2);c.button(228,12,52,"INT");c.button(290,12,54,"ESC");
  for(int r=0;r<rows;r++){auto&line=visible(r);for(int col=0;col<columns;col++){auto&v=line[col];int px=18+col*6,py=51+r*9;if(v.bg)c.rect(px,py,6,9,v.bg);if(v.ch!=' ')c.text(px,py,std::string(1,v.ch),v.fg);}}
  if(cursor&&!scroll)c.rect(18+x*6,58+y*9,5,1,3);
  c.text(16,220,status.substr(0,54),146);return c;
 }
};
struct PtySession {
 int master=-1;pid_t child=-1;bool ended=false;int exit_status=0;std::string pending;
 PtySession()=default;PtySession(const PtySession&)=delete;PtySession&operator=(const PtySession&)=delete;
 void start(const std::string&shell="/bin/sh"){
  master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);require(master>=0,"cannot open PTY; check /dev/pts");
  if(grantpt(master)||unlockpt(master)){close(master);master=-1;throw std::runtime_error("cannot initialize PTY");}
  char name[128];if(ptsname_r(master,name,sizeof(name))){close(master);master=-1;throw std::runtime_error("cannot resolve PTY");}
  winsize size{};size.ws_col=TerminalScreen::columns;size.ws_row=TerminalScreen::rows;if(ioctl(master,TIOCSWINSZ,&size)){close(master);master=-1;throw std::runtime_error("PTY size setup failed");}
  int errors[2];if(pipe2(errors,O_CLOEXEC)){close(master);master=-1;throw std::runtime_error("PTY error pipe");}
  child=fork();if(child<0){close(errors[0]);close(errors[1]);close(master);master=-1;throw std::runtime_error("cannot fork shell");}
  if(child==0){close(errors[0]);int error=0;if(setsid()<0)error=errno;int slave=error?-1:open(name,O_RDWR);if(slave<0&&!error)error=errno;if(!error&&ioctl(slave,TIOCSCTTY,0))error=errno;
   if(!error){for(int fd=0;fd<3;fd++)if(dup2(slave,fd)<0)error=errno;}if(slave>2)close(slave);close(master);
   if(!error){signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);signal(SIGPIPE,SIG_DFL);setenv("TERM","vt100",1);setenv("LC_ALL","C",1);setenv("PS1","haebancheon$ ",1);execl(shell.c_str(),shell.c_str(),"-i",static_cast<char*>(nullptr));error=errno;}
   ssize_t written;do{written=write(errors[1],&error,sizeof(error));}while(written<0&&errno==EINTR);_exit(127);
  }
  close(errors[1]);int error=0;ssize_t n;do{n=read(errors[0],&error,sizeof(error));}while(n<0&&errno==EINTR);close(errors[0]);if(n!=0){waitpid(child,nullptr,0);child=-1;close(master);master=-1;throw std::runtime_error(std::string("shell start failed: ")+strerror(error?error:EIO));}
 }
 void flush(){while(!pending.empty()){auto n=write(master,pending.data(),pending.size());if(n>0)pending.erase(0,size_t(n));else if(n<0&&errno==EINTR)continue;else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;else{pending.clear();break;}}}
 void send(const std::string&s){require(!ended&&master>=0,"shell has exited");require(pending.size()+s.size()<=4096,"terminal input queue full");pending+=s;flush();}
 bool drain(TerminalScreen&screen){bool changed=false;flush();char bytes[4096];size_t budget=65536;
  while(budget){auto n=read(master,bytes,std::min(budget,sizeof(bytes)));if(n>0){screen.feed(std::string(bytes,size_t(n)));budget-=size_t(n);changed=true;}else if(n<0&&errno==EINTR)continue;else break;}
  if(child>0){int status;pid_t r=waitpid(child,&status,WNOHANG);if(r==child){child=-1;ended=true;exit_status=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);changed=true;}}
  return changed;
 }
 ~PtySession(){if(master>=0)close(master);if(child>0){kill(-child,SIGHUP);kill(child,SIGHUP);for(int i=0;i<20;i++){if(waitpid(child,nullptr,WNOHANG)==child){child=-1;break;}poll(nullptr,0,10);}if(child>0){kill(-child,SIGKILL);kill(child,SIGKILL);waitpid(child,nullptr,0);}}}
};
inline std::string terminal_key(const std::string&key){
 if(key=="Enter")return "\r";if(key=="Tab")return "\t";if(key=="Backspace")return "\x7f";if(key=="Delete")return "\x1b[3~";if(key=="Home")return "\x1b[H";if(key=="End")return "\x1b[F";
 if(key=="ArrowUp")return "\x1b[A";if(key=="ArrowDown")return "\x1b[B";if(key=="ArrowRight")return "\x1b[C";if(key=="ArrowLeft")return "\x1b[D";if(key=="Interrupt")return "\x03";if(key=="EOF")return "\x04";if(key=="ClearScreen")return "\x0c";return "";
}
struct Desktop {
 std::string kind,catalog_path,self,status="Ready";Catalog catalog;size_t page=0;bool confirm_power=false;std::chrono::steady_clock::time_point confirm_deadline;
 TerminalScreen screen;std::unique_ptr<PtySession> pty;Children children;
 Desktop(std::string k,std::string path,std::string executable):kind(std::move(k)),catalog_path(std::move(path)),self(std::move(executable)){if(kind=="launcher")reload();}
 void reload(){try{auto next=Catalog::load(catalog_path);catalog=std::move(next);page=0;status=std::to_string(catalog.entries.size())+" apps";}catch(const std::exception&e){status=e.what();}}
 void start_terminal(){pty=std::make_unique<PtySession>();try{pty->start();status="54x18 / ASCII / Ctrl+C Ctrl+D Ctrl+L";}catch(const std::exception&e){pty.reset();status=e.what();}}
 Canvas render(){
  if(kind=="terminal")return screen.render(status);
  Canvas c;c.clear(36);c.rect(0,0,360,47,0);c.text(16,17,kind=="launcher"?"Home":"Control Center",255,2);
  if(kind=="launcher"){
   c.target("reload",270,12,74,25);c.target("previous",16,211,44,25,page>0);c.target("next",300,211,44,25,(page+1)*6<catalog.entries.size());
   for(size_t i=page*6;i<catalog.entries.size()&&i<page*6+6;i++)c.target("app:"+catalog.entries[i].id,16+int((i%6)%3)*110,55+int((i%6)/3)*77,104,72);
   c.button(270,12,74,"Reload");size_t start=page*6;
   for(size_t i=start;i<catalog.entries.size()&&i<start+6;i++){int x=16+int((i-start)%3)*110,y=55+int((i-start)/3)*77;c.rect(x,y,104,72,0);icon(c,catalog.entries[i],x+28,y+3);c.text(x+6,y+57,catalog.entries[i].title);}
   c.button(16,211,44,"<");c.button(300,211,44,">");c.text(72,220,status.substr(0,36),255);
  }else{
   c.target("front",16,60,328,66);c.target("power",16,138,328,66);
   c.rect(16,60,328,66,0);c.text(30,73,"Front center",255,2);c.text(30,102,"Reset output view to 0 / 0",146);
   c.rect(16,138,328,66,confirm_power?228:0);c.text(30,151,confirm_power?"Power off?":"Power off",255,2);c.text(30,180,confirm_power?"Press again / outside to cancel":"Shut down board Linux",255);
   c.text(16,220,status.substr(0,54),255);
  }return c;
 }
 void event(const J&e,Client*control){
  std::string type=e.value("type",std::string());
  if(kind=="terminal"){
   if(type=="key"&&(e.at("key")=="PageUp"||e.at("key")=="PageDown")){int delta=e.at("key")=="PageUp"?TerminalScreen::rows:-TerminalScreen::rows;screen.scroll=std::clamp(screen.scroll+delta,0,int(screen.history.size()));return;}
   std::string bytes;if(type=="text")bytes=e.at("text");else if(type=="key")bytes=terminal_key(e.at("key"));else if(type=="click"){int x=e.at("x"),y=e.at("y");if(app_ui::inside(x,y,228,12,52,25))bytes="\x03";else if(app_ui::inside(x,y,290,12,54,25))bytes="\x1b";}
   if(!bytes.empty()&&pty){try{pty->send(bytes);screen.scroll=0;}catch(const std::exception&ex){status=ex.what();}}return;
  }
  if(type!="click")return;int x=e.at("x"),y=e.at("y");
  if(kind=="launcher"){
   if(app_ui::inside(x,y,270,12,74,25)){reload();return;}
   if(app_ui::inside(x,y,16,211,44,25)){if(page)page--;return;}if(app_ui::inside(x,y,300,211,44,25)){if((page+1)*6<catalog.entries.size())page++;return;}
   for(size_t i=page*6;i<catalog.entries.size()&&i<page*6+6;i++){int rx=16+int((i%6)%3)*110,ry=55+int((i%6)/3)*77;if(app_ui::inside(x,y,rx,ry,104,72)){try{auto pid=children.start(catalog.entries[i].command,self);status="Started PID "+std::to_string(pid);}catch(const std::exception&ex){status=ex.what();}break;}}
  }else{
   bool power=app_ui::inside(x,y,16,138,328,66);auto now=std::chrono::steady_clock::now();
   if(!power){confirm_power=false;if(app_ui::inside(x,y,16,60,328,66)&&control){try{control->call("view_set",{{"yaw",0},{"pitch",0}});status="Front view restored";}catch(const std::exception&ex){status=ex.what();}}return;}
   if(!confirm_power||now>=confirm_deadline){confirm_power=true;confirm_deadline=now+std::chrono::seconds(5);status="Press Power off again within 5 s";return;}
   confirm_power=false;if(control){try{require(!control->call("get_state").at("service").at("mock").get<bool>(),"Power off disabled in mock mode");children.start({"/sbin/poweroff"},self);status="Shutdown requested";}catch(const std::exception&ex){status=ex.what();}}
  }
 }
 bool tick(){children.reap();bool changed=false;if(confirm_power&&std::chrono::steady_clock::now()>=confirm_deadline){confirm_power=false;status="Power off cancelled";changed=true;}if(pty&&pty->drain(screen)){changed=true;if(pty->ended)status="Shell exited: "+std::to_string(pty->exit_status);}return changed;}
};
inline int run(const std::string&kind,const std::string&catalog,const std::string&self,const std::string&output,bool once,volatile sig_atomic_t&finished){
 if(!output.empty()){Desktop app(kind,catalog,self);if(kind=="terminal"){app.screen.feed("haebancheon$ echo Hello\r\nHello\r\nhaebancheon$ ");app.status="Preview only / no shell started";}app.render().ppm(output);return 0;}
 app_ui::Window window("BoAYO "+kind,360,240);Desktop app(kind,catalog,self);std::unique_ptr<Client> control;if(kind=="control")control=std::make_unique<Client>("control-center",socket_path()+".control");
 if(kind=="terminal")app.start_terminal();window.present(app.render());std::cout<<"EXAMPLE_READY "<<J{{"app",kind},{"window_id",window.id},{"first_frame","B+content"},{"font","ASCII"}}.dump()<<std::endl;if(once)return 0;
 while(!finished){auto events=window.events(kind=="terminal"&&app.pty&&!app.pty->ended?50:250);bool changed=!events.empty();for(auto&e:events){if(e.at("window_id")!=window.id)continue;if(e.value("type",std::string())=="closed"){finished=1;break;}app.event(e,control.get());}if(finished)break;changed|=app.tick();if(changed)window.present(app.render());}return 0;
}
}
