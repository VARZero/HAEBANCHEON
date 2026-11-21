// Exercise intermediate publications directly; no timing-dependent sleeps.
#define TRIANGLE_SERVICE_ONLY
#include "../src/boayo_service.cpp"
int main(){
 std::string path="/tmp/triangle-feedback-"+std::to_string(getpid());setenv("BOAYO_SOCKET",path.c_str(),1);
 triangle_service::Server server(true);auto&e=*server.engine;
 triangle_service::Peer app{-10,false,{},{},nullptr,0},ctl{-11,true,{},{},nullptr,0};
 auto call=[&](triangle_service::Peer&p,const char*op,J a=J::object()){return server.dispatch(p,op,a);};
 e.pointer.display=false;e.pointer.visible=false;
 int id=call(app,"create_window",{{"surface_width",32},{"surface_height",32},{"width_deg",20},{"height_deg",14},{"background_index",84},{"splash_index",228}}).at("window_id");
 auto&w=e.windows.at(id);
 // A ready app must not bypass the first S publication, even in the same batch.
 w.surface.init(32,32,255);w.has_content=true;
 server.present();require(server.dirty&&server.presented<server.generation,"splash incorrectly completed content");
 require(e.stats.projected==0,"splash projected content");
 for(unsigned t:w.footprint)require(e.metadata[256+t]==e.colors.at(228)*1024,"splash color");
 server.present();require(!server.dirty&&server.presented==server.generation,"content never completed");
 require(e.stats.projected>0,"content not projected");
 for(unsigned t:w.footprint)if(!e.planned_version[t])require(e.metadata[256+t]==e.colors.at(84)*1024,"background color");
 auto clean=e.snapshot();auto original=w.footprint;
 auto button=[&](int kind){for(auto c:e.ui_controls)if(c.window==id&&c.kind==kind)return c;throw std::runtime_error("missing control");};
 auto move=button(2);
 call(ctl,"input_button",{{"x",move.x},{"y",move.y},{"pressed",true}});server.present();
 auto click=call(ctl,"input_button",{{"x",move.x},{"y",move.y},{"pressed",false}});server.present();
 require(click.at("applied")==false&&!server.dirty&&!w.shell_pending,"stationary click repainted content");
 call(ctl,"input_cancel",{{"hide",true}});server.present();

 call(ctl,"input_button",{{"x",move.x},{"y",move.y},{"pressed",true}});server.present();
 call(ctl,"input_move",{{"x",move.x+80},{"y",move.y-35}});server.present();
 require(w.footprint==original&&w.yaw==0,"preview moved real content");
 require(e.stats.projected==0&&e.stats.plan_tiles==0,"preview replanned content");
 require(!e.preview.masks.empty()&&e.preview.color==228,"missing S outline");
 for(auto &[t,mask]:e.preview.masks){
   unsigned count=0;for(auto word:mask)count+=__builtin_popcount(word);
   require(count<=96,"outline filled tile interior");
   for(unsigned row=1;row<31;row++)for(unsigned col=1;col<row;col++){
     unsigned cell=row*row+2*col;require(!(mask[cell/32]&(1u<<(cell%32))),"outline covered interior cell");
   }
 }
 call(ctl,"input_cancel",{{"hide",true}});server.present();
 require(clean==e.snapshot()&&!e.preview.window,"cancel left trail");
 call(ctl,"input_button",{{"x",move.x},{"y",move.y},{"pressed",true}});server.present();
 auto r=call(ctl,"input_button",{{"x",move.x+80},{"y",move.y-35},{"pressed",false}});
 require(r.at("applied")==true&&!e.preview.window,"release did not commit");
 server.present();require(e.stats.projected==0&&server.dirty,"release failed to show B first");
 for(unsigned t:w.footprint)require(e.metadata[256+t]==e.colors.at(84)*1024,"release background is not B");
 require(server.feedback==server.generation&&server.presented<server.generation,"feedback/content conflated");
 server.present();require(e.stats.projected>0&&!server.dirty,"release content missing");
 auto resize=button(3);double oldwidth=w.width;
 call(ctl,"input_button",{{"x",resize.x},{"y",resize.y},{"pressed",true}});server.present();
 call(ctl,"input_move",{{"x",resize.x+35},{"y",resize.y+25}});server.present();
 require(w.width==oldwidth&&e.preview.width>oldwidth,"resize did not preview independently");
 call(ctl,"input_cancel");server.present();require(w.width==oldwidth,"resize cancel changed geometry");
 // Across all split edges, generated segment coverage is complete.
 unsigned split=0;for(unsigned t=0;t<tiles;t++)for(unsigned edge=0;edge<3;edge++){
   require((tile_edge_coverage[t][edge][0]|tile_edge_coverage[t][edge][1])==UINT32_MAX,"edge coverage gap");
   split+=tile_neighbors[t][edge][1]!=65535;
 }
 require(split==780,"topology changed");
 // A view change before the first scene must not acknowledge S as final content.
 setenv("BOAYO_SOCKET",(path+"-view").c_str(),1);
 triangle_service::Server early(true);
 auto made=early.dispatch(app,"create_window",{{"surface_width",32},{"surface_height",32}});
 auto&ew=early.engine->windows.at(made.at("window_id"));ew.surface.init(32,32,255);ew.has_content=true;
 early.dispatch(ctl,"view_set",{{"yaw",2},{"pitch",0}});
 require(early.presented<early.generation,"initial view falsely completed splash");
 early.present();require(early.presented==early.generation,"initial view never completed");
 std::cout<<"PASS_FEEDBACK: S first, B before content, thin actual-tile outline, move/resize/cancel, split seams, separate completion"<<std::endl;
}
