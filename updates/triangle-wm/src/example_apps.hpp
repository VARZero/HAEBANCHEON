#pragma once
#include "app_ui.hpp"
#include <cstdio>
#include <sstream>

namespace example_apps {
using app_ui::Canvas;
struct Stats {
 uint64_t uptime=0,available_kib=0,total_kib=0,cpu_total=0,cpu_idle=0;int cpu_percent=-1;
 void sample(){
  std::ifstream up("/proc/uptime");std::string value;if(up>>value)uptime=std::stoull(value);
  std::ifstream memory("/proc/meminfo");std::string line;
  while(std::getline(memory,line)){std::istringstream row(line);std::string key;uint64_t n;if(row>>key>>n){if(key=="MemTotal:")total_kib=n;if(key=="MemAvailable:")available_kib=n;}}
  std::ifstream proc("/proc/stat");std::getline(proc,line);std::istringstream row(line);std::string cpu;row>>cpu;uint64_t fields[8]{};for(auto&n:fields)row>>n;
  uint64_t total=0;for(auto n:fields)total+=n;uint64_t idle=fields[3]+fields[4];
  if(cpu_total&&total>cpu_total&&idle>=cpu_idle&&idle-cpu_idle<=total-cpu_total)cpu_percent=int(100*(total-cpu_total-(idle-cpu_idle))/(total-cpu_total));
  cpu_total=total;cpu_idle=idle;
 }
};
struct Model {
 std::string kind,path,text,status="Ready";size_t cursor=0,scroll=0;bool modified=false,pointer=true;
 unsigned background=36,splash=228,applied_background=36;Stats stats;bool apply_colors=false,apply_pointer=false;
 static constexpr size_t document_limit=16384;static constexpr unsigned columns=53,rows=14;
 Model(std::string app,std::string filename="/tmp/boayo-note.txt"):kind(std::move(app)),path(std::move(filename)){
  require(kind=="editor"||kind=="settings"||kind=="dashboard","choose editor, settings or dashboard");
  if(kind=="editor")load();if(kind=="dashboard")stats.sample();
 }
 void load(){
  std::ifstream f(path,std::ios::binary);
  if(f){std::string bytes(document_limit+1,'\0');f.read(bytes.data(),bytes.size());bytes.resize(size_t(f.gcount()));require(bytes.size()<=document_limit&&app_ui::ascii(bytes),"editor accepts ASCII files up to 16 KiB");text=std::move(bytes);}
  else {require(access(path.c_str(),F_OK)!=0&&errno==ENOENT,"cannot read note");text="Hello, HAEBANCHEON.\nEdit this note and press Save.\n";}
  cursor=text.size();
 }
 struct Line {size_t start,length;};
 std::vector<Line> lines()const{
  std::vector<Line> out;size_t i=0;
  while(i<text.size()){size_t start=i;while(i<text.size()&&text[i]!='\n'&&i-start<columns)i++;out.push_back({start,i-start});if(i<text.size()&&text[i]=='\n')i++;}
  if(out.empty()||text.back()=='\n'||out.back().length==columns)out.push_back({text.size(),0});return out;
 }
 std::pair<size_t,size_t> position(const std::vector<Line>&ls)const{
  for(size_t i=0;i<ls.size();i++){size_t end=ls[i].start+ls[i].length;bool line_end=end<text.size()?text[end]=='\n':i+1==ls.size()||ls[i].length<columns;if(cursor>=ls[i].start&&(cursor<end||(cursor==end&&line_end)))return {i,cursor-ls[i].start};}
  return {ls.size()-1,0};
 }
 void insert(const std::string&s){
  if(!app_ui::ascii(s)||text.size()+s.size()>document_limit){status="ASCII / 16 KiB limit";return;}
  text.insert(cursor,s);cursor+=s.size();modified=true;status="Modified";
 }
 void save(){
  std::string temp=path+".boayo-"+std::to_string(getpid());int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
  if(fd<0){status="Save failed";return;}bool ok=true;size_t off=0;
  while(off<text.size()){auto n=write(fd,text.data()+off,text.size()-off);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}off+=size_t(n);}
  if(ok&&fsync(fd))ok=false;if(close(fd))ok=false;
  if(ok&&rename(temp.c_str(),path.c_str()))ok=false;
  if(!ok){unlink(temp.c_str());status="Save failed";return;}modified=false;status="Saved";
 }
 void key(const std::string&k){
  if(k=="Save"){save();return;}
  if(k=="Backspace"&&cursor){text.erase(--cursor,1);modified=true;}
  else if(k=="Delete"&&cursor<text.size()){text.erase(cursor,1);modified=true;}
  else if(k=="ArrowLeft"&&cursor)cursor--;
  else if(k=="ArrowRight"&&cursor<text.size())cursor++;
  else if(k=="Enter")insert("\n");else if(k=="Tab")insert("    ");
  else if(k=="Home"||k=="End"||k=="ArrowUp"||k=="ArrowDown"){
   auto ls=lines();auto [row,col]=position(ls);
   if(k=="Home")cursor=ls[row].start;else if(k=="End")cursor=ls[row].start+ls[row].length;
   else {if(k=="ArrowUp"&&row)row--;if(k=="ArrowDown"&&row+1<ls.size())row++;cursor=ls[row].start+std::min(col,ls[row].length);}
  }
  if(modified)status="Modified";
 }
 void event(const J&e){
  std::string type=e.value("type",std::string());
  if(kind=="editor"){
   if(type=="text")insert(e.at("text"));if(type=="key")key(e.at("key"));
   if(type=="click"){int x=e.at("x"),y=e.at("y");if(app_ui::inside(x,y,268,14,76,25))save();
    else if(app_ui::inside(x,y,16,56,328,138)){auto ls=lines();size_t row=std::min(ls.size()-1,scroll+size_t(std::max(0,y-62)/9));cursor=ls[row].start+std::min(ls[row].length,size_t(std::max(0,x-20)/6));}}
  }else if(type=="click"){
   int x=e.at("x"),y=e.at("y");
   if(kind=="settings"){
    const unsigned splashes[4]={228,3,164,255},backgrounds[4]={0,36,73,84};
    for(int i=0;i<4;i++){if(app_ui::inside(x,y,146+i*46,62,38,30))splash=splashes[i];if(app_ui::inside(x,y,146+i*46,112,38,30))background=backgrounds[i];}
    if(app_ui::inside(x,y,16,159,210,28)){pointer=!pointer;apply_pointer=true;}
    if(app_ui::inside(x,y,268,201,76,25))apply_colors=true;
   }else if(app_ui::inside(x,y,250,14,94,25))stats.sample();
  }
 }
 Canvas render(){
  Canvas c;c.clear(kind=="settings"?applied_background:background);c.rect(0,0,360,47,0);
  if(kind=="editor"){c.target("save",268,14,76,25);c.target("document",16,56,328,138);}
  else if(kind=="settings"){for(int i=0;i<4;i++){c.target("splash"+std::to_string(i),146+i*46,62,38,30);c.target("background"+std::to_string(i),146+i*46,112,38,30);}c.target("pointer",16,159,210,28);c.target("apply",268,201,76,25);}
  else c.target("refresh",250,14,94,25);
  c.text(16,17,kind=="editor"?"Notes":kind=="settings"?"Settings":"Dashboard",255,2);
  if(kind=="editor"){
   c.button(268,14,76,"Save",modified);auto ls=lines();auto [row,col]=position(ls);
   if(row<scroll)scroll=row;if(row>=scroll+rows)scroll=row-rows+1;
   c.rect(16,56,328,138,0);for(size_t i=scroll;i<ls.size()&&i<scroll+rows;i++)c.text(20,62+int(i-scroll)*9,text.substr(ls[i].start,ls[i].length));
   c.rect(20+int(col)*6,61+int(row-scroll)*9,1,9,3);
   c.text(16,204,status,modified?3:255);c.text(16,220,"Ln "+std::to_string(row+1)+" Col "+std::to_string(col+1)+" / "+std::to_string(text.size())+" bytes",146);
  }else if(kind=="settings"){
   c.text(16,73,"Splash S",255,2);c.text(16,123,"Background B",255,1);
   const unsigned a[4]={228,3,164,255},b[4]={0,36,73,84};
   for(int i=0;i<4;i++){c.rect(146+i*46,62,38,30,a[i]);c.rect(146+i*46,112,38,30,b[i]);if(splash==a[i])c.rect(151+i*46,97,28,2,255);if(background==b[i])c.rect(151+i*46,147,28,2,255);}
   c.button(16,159,210,pointer?"Pointer dot: ON":"Pointer dot: OFF",pointer);c.button(268,201,76,"Apply");c.text(16,210,"S "+std::to_string(splash)+"  B "+std::to_string(background),146);
  }else{
   c.button(250,14,94,"Refresh");c.text(16,61,"Uptime",146);c.text(16,76,std::to_string(stats.uptime)+" s",255,2);
   c.text(16,106,"CPU",146);c.text(16,121,stats.cpu_percent<0?"Sampling...":std::to_string(stats.cpu_percent)+"%",255,2);
   c.text(16,152,"Available memory",146);c.text(16,167,std::to_string(stats.available_kib/1024)+" MiB / "+std::to_string(stats.total_kib/1024)+" MiB");
   c.rect(16,186,328,8,0);if(stats.total_kib)c.rect(16,186,int(328*std::min(stats.available_kib,stats.total_kib)/stats.total_kib),8,3);
   c.text(16,212,"Live /proc sample - no simulated values",146);
  }
  return c;
 }
};
}
