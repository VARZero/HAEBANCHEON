#include "../src/example_apps.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);std::string path=std::string(argv[1])+"/note.txt";{std::ofstream f(path);}
 example_apps::Model editor("editor",path);assert(editor.text.empty());
 editor.insert("Hello!\nabc");editor.key("Backspace");assert(editor.text=="Hello!\nab");
 editor.key("ArrowLeft");editor.key("Delete");assert(editor.text=="Hello!\na");editor.key("Save");assert(!editor.modified);
 {std::ifstream f(path);std::string s((std::istreambuf_iterator<char>(f)),{});assert(s==editor.text);}
 editor.key("Home");editor.insert("z");assert(editor.modified);auto c=editor.render();assert(c.pixels.size()==360*240);
 auto old=editor.text;editor.insert(std::string(16385,'a'));assert(editor.text==old);
 editor.text=std::string(53,'x');editor.cursor=53;assert(editor.lines().size()==2);assert(editor.position(editor.lines()).first==1);editor.event({{"type","click"},{"x",20},{"y",56}});assert(editor.cursor==0);
 app_ui::Canvas lower,upper;lower.text(2,2,"hello, world!");upper.text(2,2,"HELLO, WORLD!");assert(lower.pixels!=upper.pixels);
 for(int ch=33;ch<127;ch++){const char*g=app_ui::letter(char(ch));bool any=false;for(unsigned i=0;i<14;i++)any|=g[i]!='0';assert(any);}
 example_apps::Model settings("settings");settings.event({{"type","click"},{"x",150},{"y",170}});assert(!settings.pointer&&settings.apply_pointer);
 settings.event({{"type","click"},{"x",195},{"y",70}});assert(settings.splash==3);
 settings.event({{"type","click"},{"x",290},{"y",210}});assert(settings.apply_colors);
 example_apps::Stats stats;stats.sample();assert(stats.total_kib>0&&stats.available_kib<=stats.total_kib);
 unlink(path.c_str());std::cout<<"PASS_EXAMPLE_MODELS: real save/reload, cursor edits, wrap/click, document bound, lowercase and punctuation, settings, real proc samples\n";
}
