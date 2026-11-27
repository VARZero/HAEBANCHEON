#include "../src/desktop_apps.hpp"
#include <cassert>
#include <iostream>
template<class F>void rejected(F f){bool yes=false;try{f();}catch(const std::exception&){yes=true;}assert(yes);}
int main(int argc,char**argv){
 assert(argc==3);std::string folder=argv[2];auto catalog=desktop_apps::Catalog::load(argv[1]);assert(catalog.entries.size()==5);auto json=J::parse(std::ifstream(argv[1]));
 auto invalid=[&](J value){auto path=folder+"/bad.json";{std::ofstream f(path);f<<value;}rejected([&]{desktop_apps::Catalog::load(path);});unlink(path.c_str());};
 auto bad=json;bad["apps"][0]["command"]="/bin/sh";invalid(bad);bad=json;bad["apps"][0]["command"][0]="relative";invalid(bad);bad=json;bad["apps"][0]["icon16"][0]="zzzz";invalid(bad);bad=json;bad["apps"][1]["id"]=bad["apps"][0]["id"];invalid(bad);
 desktop_apps::Children children;std::string marker=folder+"/literal.txt",literal="spaces ; $(not-executed)";
 children.start({"/bin/sh","-c","printf '%s' \"$1\" > \"$2\"","test",literal,marker},"",false);
 for(int i=0;i<100&&!children.pids.empty();i++){children.reap();poll(nullptr,0,10);}assert(children.pids.empty());
 {std::ifstream f(marker);std::string got((std::istreambuf_iterator<char>(f)),{});assert(got==literal);}unlink(marker.c_str());rejected([&]{children.start({"/does-not-exist"},"");});
 desktop_apps::TerminalScreen s;s.feed("abc\rZ");assert(s.text().substr(0,3)=="Zbc");s.feed("\x1b[2J\x1b[H\x1b[31mRED\x1b[0m");assert(s.cells[0][0].ch=='R'&&s.cells[0][0].fg==228&&s.cells[0][0].bg==0);
 s.feed("\x1b[2;3H!");assert(s.cells[1][2].ch=='!');s.feed("\x1b[?25l");assert(!s.cursor);s.feed("\x1b[?25h");assert(s.cursor);
 s.feed("\x1b[?1049hALT\x1b[?1049l");assert(s.cells[0][0].ch=='R'&&s.cells[1][2].ch=='!');
 s.reset();s.feed(std::string(54,'a'));assert(s.y==0);s.feed("b");assert(s.y==1&&s.cells[1][0].ch=='b');
 s.feed("\x1b]0;ignored title\x1b\\OK");assert(s.cells[1][1].ch=='O');
 s.feed("\x1b["+std::string(20000,';'));s.feed("\x1b[2J\x1b[Hsafe");assert(s.cells[0][0].ch=='s');
 for(int i=0;i<400;i++)s.feed("\r\nline");assert(s.history.size()==200);s.scroll=200;assert(s.render("history").pixels.size()==86400);
 s.reset();s.feed("\x1b[2;5r\x1b[5;1Hbottom\r\nnext");assert(s.cells[3][0].ch=='b'&&s.cells[4][0].ch=='n');
 assert(desktop_apps::terminal_key("Interrupt")=="\x03"&&desktop_apps::terminal_key("ArrowUp")=="\x1b[A");
 pid_t shell;
 {
  desktop_apps::PtySession p;p.start();shell=p.child;desktop_apps::TerminalScreen terminal;
  auto await=[&](const std::string&token){for(int i=0;i<500;i++){p.drain(terminal);if(terminal.text().find(token)!=std::string::npos)return;poll(nullptr,0,10);}std::cerr<<terminal.text();assert(false);};
  p.send("printf 'PTY_%s\\n' READY\rstty size\r");await("PTY_READY");await("18 54");
  p.send("sleep 10\r");poll(nullptr,0,200);p.send("\x03");p.send("printf 'AFTER_%s\\n' INT\r");await("AFTER_INT");
  p.send("exit 7\r");for(int i=0;i<500&&!p.ended;i++){p.drain(terminal);poll(nullptr,0,10);}assert(p.ended&&p.exit_status==7);
 }
 assert(kill(shell,0)<0&&errno==ESRCH);
 {desktop_apps::PtySession badpty;rejected([&]{badpty.start("/no-shell");});assert(badpty.master<0&&badpty.child<0);}
 std::cout<<"PASS_DESKTOP_MODELS: catalog validation, literal argv, exec failure, bounded VT parser/history, real PTY shell, winsize, interrupt, exit and cleanup\n";
}
