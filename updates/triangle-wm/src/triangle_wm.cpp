#include "geometry.hpp"
#include "bosio_uapi.h"
#include "tile_geometry.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <climits>
#include <functional>
#include "content.hpp"
using namespace boayo;
#include "fast_camera.hpp"
#include "fast_projection.hpp"
#include "texture_cache.hpp"
#include "window_transform.hpp"
#include "json_object.hpp"
#include "outline.hpp"
#include "ui_types.hpp"
#include "ui_bounds.hpp"
#include "ui_samples.hpp"
static uint64_t microseconds_now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static J summary(std::vector<double> a){require(!a.empty(),"empty samples");auto raw=a;std::sort(a.begin(),a.end());return {{"n",a.size()},{"median_ms",a[a.size()/2]},{"p95_ms",a[(a.size()*95+99)/100-1]},{"max_ms",a.back()},{"samples_ms",raw}};}
struct TilePlan {Descriptor descriptor;uint64_t version=0;};
struct TilePlans : std::vector<std::pair<unsigned,TilePlan>> {
 auto find(unsigned id){auto i=std::lower_bound(begin(),end(),id,[](const auto &v,unsigned t){return v.first<t;});return i!=end()&&i->first==id?i:end();}
 TilePlan &at(unsigned id){auto i=find(id);require(i!=end(),"missing tile plan");return i->second;}
};
struct Window {int id;double yaw=0,pitch=0,width=20,height=14,padding=2;unsigned color=84,splash_color=228;bool shell_pending=false,splash_pending=false;bool visible=true,has_content=false,plan_ready=false;std::vector<unsigned> footprint;Surface surface;TilePlans plan;WindowTransform transform;};
struct RenderStats {
 bool published=false;unsigned changed=0,content_changed=0,owned=0,projected=0,copied=0,uploaded=0,scatter=0,evictions=0,resident=0,bank=0;
 unsigned plan_tiles=0,plan_rejected=0,plan_fallbacks=0;std::array<unsigned,2> jobs{};
 double plan_ms=0,projection_ms=0,render_ms=0,publish_ms=0;bo_publish_profile profile{};
};
struct Engine {
 // Checkpoints only accept input; the final checkpoint may update the overlay.
 std::function<void(bool)> input_checkpoint;
 RenderStats stats;
 bool ui_enabled=false,ui_dirty=true,ui_layout_dirty=true,ui_mask_dirty=true,ui_cold_view=true;UiPointer pointer;
 std::map<unsigned,UiPaint> ui_paints;
 UiPreview preview;
 std::map<unsigned,UiEdgeMask> ui_edge_masks;
 bool ui_delta=true;
 bool tile_preview=true;MoveMarker move_marker;
 uint64_t plan_builds=0,footprint_calls=0,run_submissions=0;
 unsigned ui_rebuilt=0,ui_incremental=0,ui_reused=0,ui_written_cells=0,ui_restored_cells=0,ui_copy_bytes=0;
 uint64_t ui_mask_reused=0,ui_mask_built=0;
 uint64_t ui_layout_us=0,ui_pick_us=0,ui_masks_us=0,ui_payload_us=0;
 std::map<unsigned,UiButtonCache> ui_button_cache;
 std::map<unsigned,UiInverse> ui_inverses;
 std::map<unsigned,UiSamples> ui_samples;
 std::array<uint32_t,180> mock_camera{};bool mock_configured=false;
 std::vector<UiControl> ui_controls;std::map<unsigned,unsigned> ui_slots;
 std::array<std::array<UiStamp,256>,2> ui_stamps{};
 Words display_metadata,ui_metadata;double ui_ms=0;unsigned ui_tile_count=0;
 double view_yaw=0,view_pitch=0;std::array<V,3> view_axes=axes(0,0);
 std::unique_ptr<Geometry::Projection> ui_projection;std::unique_ptr<FastProjection> ui_transform;
 std::map<unsigned,Descriptor> ui_descriptors;
 bool trim_background=true;
 double plan_ms=0;unsigned plan_tiles=0,plan_fallbacks=0,plan_rejected=0;
 std::array<Descriptor,128> descriptor_batch{};
 Words scratch_metadata;std::vector<unsigned> scratch_touched,scratch_visible;std::vector<bo_batch_entry> copy_batch;
 bool mock;int fd=-1,active=-1;Geometry geometry;std::array<int32_t,180> camera;Words metadata;std::map<int,Window> windows;std::vector<int> order;
 std::map<unsigned,unsigned> colors;unsigned next_slot=0;uint64_t commits=0,pose_count=0,fill_bytes=0;bo_publish_profile profile{};
 uint64_t serial=0;std::array<int,tiles> slots;std::vector<unsigned> free_slots;std::array<uint64_t,tiles> presented{};std::array<std::array<uint64_t,tiles>,2> versions{};TextureCache texture_cache;Words texture_stage;std::vector<bo_batch_entry> texture_scatter;bool sparse_upload=false;bool failed=false;std::array<Words,BO_REGIONS> mock_memory;
 std::array<int,tiles> planned_owner{};std::array<uint64_t,tiles> planned_version{};std::array<uint8_t,tiles> touched_flag{};std::vector<unsigned> visible_tiles;
 Engine(bool m):mock(m),camera(geometry.camera()),metadata(header,UINT32_MAX){
  if(const char *v=getenv("TRIANGLE_UI_DELTA"))ui_delta=std::string(v)!="0";
  if(const char *v=getenv("TRIANGLE_PREVIEW")){require(std::string(v)=="tile"||std::string(v)=="outline","preview mode must be tile or outline");tile_preview=std::string(v)=="tile";}
  if(const char *v=getenv("TRIANGLE_OUTLINE")){require(std::string(v)=="raw"||std::string(v)=="trim","outline mode must be raw or trim");trim_background=std::string(v)=="trim";}
  if(const char *v=getenv("TRIANGLE_SPARSE_UPLOAD"))sparse_upload=std::string(v)!="0";
  if(mock)if(const char *v=getenv("TRIANGLE_TEXTURE_WORDS")){unsigned n=std::stoul(v);require(n>=1024&&n<=1048576,"mock texture capacity");texture_cache.capacity=n;}
  slots.fill(-1);if(mock){mock_memory[BO_TEXTURE].resize(1048576);mock_memory[BO_SCRATCH].resize(BO_SCRATCH_BYTES/4);mock_memory[BO_DESC0].resize(1152);mock_memory[BO_DESC1].resize(1152);mock_memory[BO_SCENE0].resize(BO_MAX_WORDS);mock_memory[BO_SCENE1].resize(BO_MAX_WORDS);}
  for(unsigned i=0;i<256;i++)metadata[i]=palette(i);
  if(!mock){fd=open("/dev/bosio0",O_RDWR|O_CLOEXEC);require(fd>=0,"open /dev/bosio0");bo_info i{};call(BO_GET_INFO,&i);require(i.abi==3&&i.signature==0x42533235u&&!i.poisoned,"healthy BS25 ABI3 required");call(BO_STOP,nullptr);}
 }
 ~Engine(){if(fd>=0){ioctl(fd,BO_STOP);close(fd);}}
 void call(unsigned long op,void *p){
  if(op==BO_RUN)++run_submissions;
  require(!failed,"hardware failure latched; restart after inspecting BOSIO status");
  if(!mock){if(ioctl(fd,op,p)<0){failed=true;throw std::runtime_error("ioctl "+std::to_string(op)+": "+strerror(errno));}return;}
  auto writable=[&](unsigned region,unsigned offset,unsigned bytes){require(region<BO_REGIONS&&uint64_t(offset)+bytes<=mock_memory[region].size()*4,"mock memory bounds");require(active<0||region!=BO_SCENE0+unsigned(active),"active bank write");};
  if(op==BO_MEM_FILL){auto &v=*static_cast<bo_mem_fill*>(p);writable(v.region,v.offset,v.bytes);std::memset(reinterpret_cast<char*>(mock_memory[v.region].data())+v.offset,v.value,v.bytes);}
  else if(op==BO_MEM_COPY){auto&v=*static_cast<bo_mem_copy*>(p);writable(v.dst_region,v.dst_offset,v.bytes);require(uint64_t(v.src_offset)+v.bytes<=mock_memory[v.src_region].size()*4,"copy source bounds");std::memcpy(reinterpret_cast<char*>(mock_memory[v.dst_region].data())+v.dst_offset,reinterpret_cast<char*>(mock_memory[v.src_region].data())+v.src_offset,v.bytes);}
  else if(op==BO_UPLOAD){auto&v=*static_cast<bo_upload*>(p);writable(v.region,v.offset,v.bytes);std::memcpy(reinterpret_cast<char*>(mock_memory[v.region].data())+v.offset,reinterpret_cast<void*>(uintptr_t(v.data)),v.bytes);}
  else if(op==BO_BATCH){auto&v=*static_cast<bo_batch*>(p);auto*e=reinterpret_cast<bo_batch_entry*>(uintptr_t(v.entries));for(unsigned i=0;i<v.count;i++){if(e[i].kind==BO_BATCH_COPY)call(BO_MEM_COPY,&e[i].op.copy);else if(e[i].kind==BO_BATCH_FILL)call(BO_MEM_FILL,&e[i].op.fill);else if(e[i].kind==BO_BATCH_BLEND)call(BO_TILE_BLEND,&e[i].op.blend);else throw std::runtime_error("unsupported mock batch");}}
  else if(op==BO_TILE_BLEND){auto&v=*static_cast<bo_tile_blend*>(p);require(v.mode<=1,"mock blend mode");require(v.mode||(v.scratch_word>=header&&v.scratch_word+256<=mock_memory[BO_SCRATCH].size()),"mock blend scratch bounds");unsigned offset=(header+v.tile*256)*4;writable(BO_SCENE0+v.bank,offset,1024);auto*bytes=reinterpret_cast<uint8_t*>(mock_memory[BO_SCENE0+v.bank].data())+offset;for(unsigned i=0;i<1024;i++)if(v.mask[i/32]&(1u<<(i%32)))bytes[i]=v.mode?v.color:reinterpret_cast<const uint8_t*>(mock_memory[BO_SCRATCH].data()+v.scratch_word)[i];}
  else if(op==BO_RUN){auto&v=*static_cast<bo_run*>(p);std::set<unsigned> targets;for(unsigned core=0;core<2;core++)for(unsigned i=0;i<v.jobs[core];i++){Descriptor d;std::copy_n(mock_memory[BO_DESC0+core].begin()+i*18,18,d.begin());require(uint64_t(d[0])+uint64_t(d[3]-1)*d[4]+d[2]<=v.texture_words && v.texture_words<=texture_cache.capacity,"texture descriptor bounds");require(targets.insert(d[1]).second,"overlapping projector destinations");writable(BO_SCENE0+v.bank,d[1]*4,1024);v.cells[core]+=project_reference(mock_memory[BO_TEXTURE],mock_memory[BO_SCENE0+v.bank],d);}}
  else if(op==BO_PUBLISH){auto&v=*static_cast<bo_publish*>(p);writable(BO_SCENE0+v.present.bank,0,header*4);require(!mock_configured||!std::memcmp(mock_camera.data(),v.present.camera,sizeof(v.present.camera)),"pose must be changed with BO_SET_POSE before publication");std::copy_n(v.present.camera,180,mock_camera.begin());mock_configured=true;std::copy_n(reinterpret_cast<uint32_t*>(uintptr_t(v.metadata)),header,mock_memory[BO_SCENE0+v.present.bank].begin());}
  else if(op==BO_SET_POSE){auto&v=*static_cast<bo_pose*>(p);std::copy_n(v.camera,180,mock_camera.begin());}
 }
 unsigned solid(unsigned c){require(c<256,"palette index");if(colors.count(c))return colors.at(c);require(next_slot<64,"solid palette capacity");unsigned s=next_slot++;colors[c]=s;
  for(unsigned b=0;b<2;b++){// Stop/initialization only: never fill a displayed bank.
   require(active<0,"new background colors must be reserved before first presentation");bo_mem_fill f{BO_SCENE0+b,(header+s*256)*4,1024,c,0};call(BO_MEM_FILL,&f);fill_bytes+=1024;
  }return s;
 }
 void reserve_window_colors(){
  require(active<0&&next_slot==0,"reserve palette before first scene");
  std::vector<bo_batch_entry> ops;ops.reserve(512);
  for(unsigned c=0;c<256;c++){unsigned slot=next_slot++;colors[c]=slot;
   for(unsigned b=0;b<2;b++){bo_batch_entry e{};e.kind=BO_BATCH_FILL;e.op.fill={BO_SCENE0+b,(header+slot*256)*4,1024,c,0};ops.push_back(e);}
  }
  run_batches(ops);fill_bytes+=512*1024;
 }
 static constexpr auto coarse_bounds=[](){
  std::array<std::array<int16_t,6>,33> out{};
  for(unsigned c=0;c<33;c++){
   for(unsigned k=0;k<3;k++){out[c][k]=32767;out[c][k+3]=-32767;}
   for(unsigned g=c*4;g<std::min(132u,(c+1)*4);g++)for(unsigned k=0;k<3;k++){out[c][k]=std::min(out[c][k],group_bounds[g][k]);out[c][k+3]=std::max(out[c][k+3],group_bounds[g][k+3]);}
  }return out;
 }();
 std::vector<unsigned> footprint(Window&w){
  ++footprint_calls;
  w.transform.update(w.yaw,w.pitch,w.width,w.height,w.padding);const auto &ax=w.transform.ax;double tx=w.transform.padded_x,ty=w.transform.padded_y;
  std::array<V,5> normal{ax[0],ax[0]*tx+ax[1],ax[0]*tx-ax[1],ax[0]*ty+ax[2],ax[0]*ty-ax[2]};int32_t plane[5][3];
  for(int p=0;p<5;p++){double largest=0;for(double v:normal[p])largest=std::max(largest,std::abs(v));for(int k=0;k<3;k++)plane[p][k]=int32_t(std::lround(normal[p][k]/largest*8192));}
  std::vector<unsigned> result;
  // Conservative plane rejection keeps whole edge tiles; no cell masks.
  for(unsigned coarse=0;coarse<33;coarse++){bool covered=true;for(int p=0;p<5&&covered;p++){int32_t upper=0;for(int k=0;k<3;k++)upper+=plane[p][k]*coarse_bounds[coarse][k+(plane[p][k]>=0?3:0)];covered=upper>=-32768;}if(!covered)continue;
  for(unsigned group=coarse*4;group<std::min(132u,(coarse+1)*4);group++){bool keep=true;for(int p=0;p<5&&keep;p++){int32_t upper=0;for(int k=0;k<3;k++)upper+=plane[p][k]*group_bounds[group][k+(plane[p][k]>=0?3:0)];keep=upper>=-32768;}if(!keep)continue;
   for(unsigned t=group*32;t<std::min(tiles,(group+1)*32);t++){bool hit=true;for(int p=0;p<5&&hit;p++){int32_t upper=INT32_MIN;for(int v=0;v<3;v++){int32_t d=0;for(int k=0;k<3;k++)d+=plane[p][k]*tile_vertex[t][v][k];upper=std::max(upper,d);}hit=upper>=-32768;}if(hit)result.push_back(t);}
  }}
  if(trim_background){
   // Only tip candidates need the unpadded test; avoid a second full mesh scan.
   std::array<V,5> content_normal{ax[0],ax[0]*(w.transform.tx/2)+ax[1],ax[0]*(w.transform.tx/2)-ax[1],ax[0]*(w.transform.ty/2)+ax[2],ax[0]*(w.transform.ty/2)-ax[2]};
   int32_t content_plane[5][3];
   for(int p=0;p<5;p++){double largest=0;for(double v:content_normal[p])largest=std::max(largest,std::abs(v));for(int k=0;k<3;k++)content_plane[p][k]=int32_t(std::lround(content_normal[p][k]/largest*8192));}
   trim_outline(result,[&](unsigned t){
    for(int p=0;p<5;p++){
     int32_t upper=INT32_MIN;
     for(int v=0;v<3;v++){int32_t d=0;for(int k=0;k<3;k++)d+=content_plane[p][k]*tile_vertex[t][v][k];upper=std::max(upper,d);}
     if(upper < -32768)return false;
    }
    return true;
   });
  }
  return result;
 }
#include "ui_render.inc"
#include "move_marker.inc"
#include "content_render.inc"
 J operate(const J&q){double begin=now();std::string op=q.at("op");J result;
  if(op=="create") {int id=q.at("id");require(id>0&&!windows.count(id),"unique positive window id");Window w{};w.id=id;w.yaw=q.value("yaw",0.);w.pitch=q.value("pitch",0.);w.width=q.value("width",20.);w.height=q.value("height",14.);w.padding=q.value("padding",2.);w.color=q.value("color",84u);validate(w);require(colors.count(w.color),"color not reserved");w.footprint=footprint(w);windows.emplace(id,w);order.push_back(id);result=render();}
  else if(op=="move"){auto &w=windows.at(q.at("id").get<int>());double y=q.at("yaw"),p=q.at("pitch");Window bounds{};bounds.yaw=y;bounds.pitch=p;validate(bounds);if(w.yaw!=y||w.pitch!=p){w.yaw=y;w.pitch=p;w.footprint=footprint(w);w.plan_ready=false;}result=render();}
  else if(op=="visible"){windows.at(q.at("id").get<int>()).visible=q.at("visible");result=render();}
  else if(op=="raise"){int id=q.at("id");require(windows.count(id),"window missing");order.erase(std::remove(order.begin(),order.end(),id),order.end());order.push_back(id);result=render();}
  else if(op=="close"){int id=q.at("id");require(windows.erase(id)==1,"window missing");order.erase(std::remove(order.begin(),order.end(),id),order.end());texture_cache.erase(id);result=render();}
  else if(op=="clear"){windows.clear();order.clear();texture_cache.clear();result=render();}
  else if(op=="content"){result=attach(windows.at(q.at("id").get<int>()),q);}
  else if(op=="component"||op=="remove_component"){auto&w=windows.at(q.at("id").get<int>());require(w.has_content,"attach content first");DamageRect d=op=="component"?w.surface.put(q.at("component")):w.surface.erase(q.at("component_id"));damage(w,d);result=render();}
  else if(op=="snapshot"){result=snapshot();}
  else if(op=="pose"){require(active>=0,"present a scene first");double y=q.value("yaw",0.),p=q.value("pitch",0.);require(std::isfinite(y)&&std::abs(y)<=180&&std::isfinite(p)&&std::abs(p)<=85,"pose bounds");auto c=fast_camera(y,p);set_ui_view(y,p);result=set_pose(c);}
  else if(op=="pose_sweep"){unsigned n=q.value("samples",61u);require(n>=3&&n<=361,"pose sample bounds");double amp=q.value("amplitude",8.);require(std::isfinite(amp)&&amp>=0&&amp<=30,"pose amplitude");std::vector<std::array<int32_t,180>> frames;double setup=now();for(unsigned i=0;i<n;i++){double phase=2*pi*i/(n-1);frames.push_back(fast_camera(amp*std::sin(phase),amp*.4*std::sin(2*phase)));}double prep=(now()-setup)*1000;std::vector<double> times;for(unsigned i=0;i<frames.size();i++){double phase=2*pi*i/(n-1);set_ui_view(amp*std::sin(phase),amp*.4*std::sin(2*phase));double t=now();set_pose(frames[i]);times.push_back((now()-t)*1000);usleep(16667);}set_ui_view(0,0);set_pose(geometry.camera());result=summary(times);result["preparation_ms"]=prep;result["scope"]="precomputed camera submit to output acknowledgment; excludes pacing";}
  else if(op=="status"){result={{"windows",windows.size()},{"commits",commits},{"poses",pose_count},{"fill_bytes",fill_bytes},{"mock",mock},{"active_bank",active}};}
  else throw std::runtime_error("unknown operation");
  result["operation"]=op;result["total_ms"]=(now()-begin)*1000;return result;
 }
 static void validate(const Window&w){require(std::isfinite(w.yaw)&&std::abs(w.yaw)<=180&&std::isfinite(w.pitch)&&std::abs(w.pitch)<=80&&std::isfinite(w.width)&&w.width>=1&&w.width<=100&&std::isfinite(w.height)&&w.height>=1&&w.height<=100&&std::isfinite(w.padding)&&w.padding>=0&&w.padding<=10,"window bounds");}
 J set_pose(const std::array<int32_t,180>&c){bo_pose p{};p.timeout_ms=30000;std::copy(c.begin(),c.end(),p.camera);double t=now();call(BO_SET_POSE,&p);camera=c;if(ui_enabled){ui_dirty=true;render(false);}pose_count++;return {{"pose_ms",(now()-t)*1000},{"scene_commits",commits}};}
};
#ifndef TRIANGLE_ENGINE_ONLY
static void send_all(int fd,const std::string&s){size_t off=0;while(off<s.size()){auto n=write(fd,s.data()+off,s.size()-off);require(n>0,"socket write");off+=n;}}
static std::string line(int fd){std::string s;char buffer[4096];while(s.size()<65536){auto n=read(fd,buffer,std::min(sizeof(buffer),65536-s.size()));if(n==0)break;if(n<0){if(errno==EINTR)continue;throw std::runtime_error("socket read");}auto end=std::find(buffer,buffer+n,'\n');s.append(buffer,end);if(end!=buffer+n)break;}require(s.size()<65536,"request too large");return s;}
int main(int argc,char **argv){try{
 signal(SIGPIPE,SIG_IGN);
 if(argc>1&&std::string(argv[1])=="--check-camera"){Geometry g;int worst=0;for(int y=-180;y<=180;y+=7)for(int p=-80;p<=80;p+=7){auto ref=g.camera(y,p),fast=fast_camera(y,p);for(unsigned i=0;i<180;i++)worst=std::max(worst,std::abs(ref[i]-fast[i]));}require(worst<=2,"fixed camera error exceeds two Q24 units");std::cout<<"PASS_CAMERA_Q24_MAX_ERROR "<<worst<<std::endl;return 0;}
 const char *socket_path="/tmp/triangle-wm.sock";
 if(argc==3&&std::string(argv[1])=="--request"){int fd=socket(AF_UNIX,SOCK_STREAM,0);sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,socket_path);require(connect(fd,(sockaddr*)&a,sizeof(a))==0,"connect WM");send_all(fd,std::string(argv[2])+"\n");auto answer=line(fd);std::cout<<answer<<std::endl;close(fd);return J::parse(answer).contains("error")?1:0;}
 bool mock=argc>1&&std::string(argv[1])=="--mock";Engine e(mock);for(unsigned color:{84u,164u,109u,38u,228u,255u})e.solid(color);
 if((argc>1&&std::string(argv[1])=="--stdin") || mock){std::string s;while(std::getline(std::cin,s)){try{std::cout<<e.operate(J::parse(s)).dump()<<std::endl;}catch(const std::exception&x){std::cout<<J{{"error",x.what()}}.dump()<<std::endl;}}return 0;}
 int server=socket(AF_UNIX,SOCK_STREAM,0);require(server>=0,"socket");sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,socket_path);require(access(socket_path,F_OK)!=0,"socket already exists; stop prior owner first");require(bind(server,(sockaddr*)&a,sizeof(a))==0&&listen(server,8)==0,"bind/listen");std::cout<<"TRIANGLE_WM_READY"<<std::endl;
 while(true){int client=accept(server,nullptr,nullptr);if(client<0){if(errno==EINTR)continue;break;}try{auto s=line(client);if(!s.empty())send_all(client,e.operate(J::parse(s)).dump()+"\n");}catch(const std::exception&x){send_all(client,J{{"error",x.what()}}.dump()+"\n");}close(client);}
 close(server);unlink(socket_path);
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}

#endif
