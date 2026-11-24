 // Construct only boundary cells, using the same footprint as the final window.
 bool ui_preview(Window &proposal,int id,unsigned color){
  auto ids=footprint(proposal);
  if(preview.window==id&&preview.color==color&&preview.footprint==ids){preview.yaw=proposal.yaw;preview.pitch=proposal.pitch;preview.width=proposal.width;preview.height=proposal.height;return false;}
  preview={};preview.window=id;preview.color=color;preview.yaw=proposal.yaw;preview.pitch=proposal.pitch;preview.width=proposal.width;preview.height=proposal.height;preview.footprint=std::move(ids);
  std::array<uint8_t,tiles> inside{};for(unsigned t:preview.footprint)inside[t]=1;
  for(unsigned t:preview.footprint){
   std::array<uint32_t,3> exposed{};
   for(unsigned edge=0;edge<3;edge++)for(unsigned n=0;n<2;n++){unsigned other=tile_neighbors[t][edge][n];if(other!=65535&&!inside[other])exposed[edge]|=tile_edge_coverage[t][edge][n];}
   if(!(exposed[0]|exposed[1]|exposed[2]))continue;
   auto &cached=ui_edge_masks[t];
   if(cached.exposed==exposed)++ui_mask_reused;
   else {
    cached.exposed=exposed;cached.mask={};++ui_mask_built;
    for(unsigned edge=0;edge<3;edge++)for(unsigned k=0;k<32;k++)if(exposed[edge]&(1u<<k)){
     unsigned row=edge==0?k:edge==1?31:31-k,col=edge==0?0:edge==1?k:row;
     unsigned cell=row*row+2*col;cached.mask[cell/32]|=1u<<(cell%32);
    }
   }
   preview.masks[t]=cached.mask;
  }
  ui_mask_dirty=true;ui_dirty=true;return true;
 }
 void ui_clear_preview(){if(preview.window){preview={};ui_mask_dirty=true;ui_dirty=true;}}
 void set_ui_view(double y,double p){
  if(view_yaw==y&&view_pitch==p)return;
  view_yaw=y;view_pitch=p;view_axes=axes(y,p);ui_projection.reset();ui_transform.reset();ui_descriptors.clear();ui_inverses.clear();ui_samples.clear();ui_cold_view=true;ui_dirty=true;ui_layout_dirty=true;ui_mask_dirty=true;
 }
 V ui_ray(int x,int y)const{
  static bool cached=!(getenv("TRIANGLE_MATH_OPT")&&std::string(getenv("TRIANGLE_MATH_OPT"))=="0");
  if(cached){
   static const double horizontal=2*std::tan(rad(24.))/1280,vertical=2*std::tan(rad(18.))/720;
   return view_axes[0]+view_axes[1]*((x-639.5)*horizontal)+view_axes[2]*((359.5-y)*vertical);
  }
  return view_axes[0]+view_axes[1]*((x-639.5)*(2*std::tan(rad(24.))/1280))+
    view_axes[2]*((359.5-y)*(2*std::tan(rad(18.))/720));
 }
 unsigned ui_pick(int x,int y)const{
  return ui_pick_ray(ui_ray(x,y));
 }
 unsigned ui_pick_ray(const V&ray)const{
  int64_t q[3];for(int k=0;k<3;k++)q[k]=std::llround(ray[k]*0x1p30);
  for(unsigned f=0;f<20;f++){
   int64_t b[3]{};for(int j=0;j<3;j++){for(int k=0;k<3;k++)b[j]+=int64_t(camera_inverse[f][j][k])*q[k];b[j]>>=20;}
   if(*std::min_element(b,b+3)<-128)continue;
   for(auto &v:b)v=std::max(int64_t(0),v);
   int64_t sum=b[0]+b[1]+b[2];if(!sum)continue;
   unsigned region=3;for(unsigned j=0;j<3;j++)if(b[j]*2>=sum){region=j;break;}
   int64_t local[3];for(unsigned j=0;j<3;j++)local[j]=std::max(int64_t(0),region==3?sum-2*b[j]:2*b[j]-(j==region?sum:0));
   unsigned n=region==3?8:7;int64_t total=(local[1]+local[2])*n,side=local[2]*n;
   unsigned row=std::min(n-1,unsigned(total/sum)),col=std::min(row,unsigned(side/sum));
   unsigned inv=side%sum>total%sum;unsigned t=std::min(n*n-1,row*row+2*col+inv);
   return f*211+(region==3?147:region*49)+t;
  }
  return tiles;
 }
 const Descriptor& ui_descriptor(unsigned t){
  auto it=ui_descriptors.find(t);if(it!=ui_descriptors.end())return it->second;
  if(!ui_projection){ui_projection=std::make_unique<Geometry::Projection>(1280,720,view_yaw,view_pitch,0,48,36);ui_transform=std::make_unique<FastProjection>(*ui_projection);}
  unsigned unused=0;auto d=fast_descriptor(geometry,t,*ui_projection,*ui_transform,unused).first;
  return ui_descriptors.emplace(t,d).first->second;
 }
 std::pair<int,int> ui_center(unsigned t){
  auto &d=ui_descriptor(t);int64_t u=int32_t(d[6])+21LL*int32_t(d[7])+10LL*int32_t(d[8]);
  int64_t v=int32_t(d[10])+21LL*int32_t(d[11])+10LL*int32_t(d[12]);
  int64_t z=int32_t(d[14])+21LL*int32_t(d[15])+10LL*int32_t(d[16]);
  if(z<=0)return {-10000,-10000};
  return {int(u/z),int(v/z)};
 }
 void ui_layout(){
  ui_controls.clear();
  for(int id:order){auto&w=windows.at(id);if(!w.visible||(move_marker.active&&move_marker.kind==3&&!move_marker.committing&&id==move_marker.window))continue;
   int32_t a[3][3];for(int j=0;j<3;j++)for(int k=0;k<3;k++)a[j][k]=std::lround(w.transform.ax[j][k]*8192);
   int tx=int(w.transform.tx*2048),ty=int(w.transform.ty*2048),pad=std::max(80,int(w.padding*72));
   unsigned best[3]={tiles,tiles,tiles};int64_t score[3]={INT64_MAX,INT64_MAX,INT64_MAX};
   unsigned second_move=tiles;int64_t second_score=INT64_MAX;
   for(unsigned t:w.footprint){
    if(planned_owner[t]!=id)continue;
    int64_t b[3]{};for(int j=0;j<3;j++)for(int k=0;k<3;k++)for(int v=0;v<3;v++)b[j]+=int64_t(a[j][k])*tile_vertex[t][v][k];
    if(b[0]<=0)continue;
    int x=int(b[1]*4096/b[0]),y=int(b[2]*4096/b[0]);
    int wantx[3]={0,0,tx},wanty[3]={ty+pad,-ty-pad,-ty-pad};
    for(unsigned k=0;k<3;k++){
     if((k==0&&y<=ty)||(k!=0&&y>=-ty)||(k==2&&x<tx/2))continue;
     int64_t dx=x-wantx[k],dy=y-wanty[k],s=dx*dx+dy*dy;
     if(s<score[k]){if(k==1){second_move=best[k];second_score=score[k];}score[k]=s;best[k]=t;}
     else if(k==1&&s<second_score){second_move=t;second_score=s;}
    }
   }
   if(best[1]==best[2])best[1]=second_move;
   for(unsigned k=0;k<3;k++)if(best[k]<tiles){
    bool used=false;for(auto&c:ui_controls)used|=c.tile==best[k];if(used)continue;
    auto [x,y]=ui_center(best[k]);if(x<16||x>1263||y<16||y>703)continue;
    ui_controls.push_back({id,int(k+1),x,y,best[k]});
   }
  }
 }
 UiControl ui_hit(int x,int y)const{
  unsigned t=ui_pick(x,y);if(t>=tiles)return {};
  for(auto it=ui_controls.rbegin();it!=ui_controls.rend();++it)
   if(planned_owner[t]==it->window&&std::abs(x-it->x)<=16&&std::abs(y-it->y)<=16)return *it;
  return {};
 }
 bool ui_pointer_changed(const UiPointer&before)const{
  bool was_drawn=before.visible&&before.display,drawn=pointer.visible&&pointer.display;
  if(was_drawn!=drawn||(drawn&&(before.x!=pointer.x||before.y!=pointer.y||before.down!=pointer.down)))return true;
  for(const auto&c:ui_controls){
   bool old_hover=before.visible&&std::abs(before.x-c.x)<=16&&std::abs(before.y-c.y)<=16;
   bool hover=pointer.visible&&std::abs(pointer.x-c.x)<=16&&std::abs(pointer.y-c.y)<=16;
   if(old_hover!=hover||(hover&&before.down!=pointer.down))return true;
  }
  return false;
 }
 void ui_compose(Words& base,unsigned bank){
  double start=now();uint64_t phase=microseconds_now();ui_pick_us=0;
  if(ui_layout_dirty){ui_button_cache.clear();ui_layout();ui_layout_dirty=false;ui_mask_dirty=true;}
  ui_layout_us=microseconds_now()-phase;phase=microseconds_now();auto &paint=ui_paints;
  if(ui_mask_dirty){paint.clear();
  for(auto&c:ui_controls)paint[c.tile];
  std::set<unsigned> cursor;
  if(pointer.visible&&pointer.display&&(!move_marker.active||move_marker.committing)){uint64_t pick_start=microseconds_now();unsigned t=ui_pick(pointer.x,pointer.y);ui_pick_us=microseconds_now()-pick_start;if(t<tiles)cursor.insert(t);
   for(int round=0;round<2;round++){auto previous=cursor;for(unsigned v:previous)for(auto&e:tile_neighbors[v]){cursor.insert(e[0]);if(e[1]!=65535)cursor.insert(e[1]);}}
   for(unsigned v:cursor)paint[v];
  }
  int nearest=INT_MAX;unsigned near_tile=tiles,near_cell=0;bool white=false;
  for(auto &[t,p]:paint){if(input_checkpoint)input_checkpoint(false);auto&d=ui_descriptor(t);
   bool pointer_tile=pointer.visible&&pointer.display&&cursor.count(t)&&affected(d,{pointer.x-12,pointer.y-12,25,25});
   std::vector<UiControl> buttons;for(auto&c:ui_controls)if(c.tile==t)buttons.push_back(c);
   if(!pointer_tile&&buttons.empty())continue;
   UiButtonCache *cached_button=nullptr;unsigned button_state=0;
   if(!pointer_tile&&!buttons.empty()){
    const auto &button=buttons.front();
    bool hover=pointer.visible&&std::abs(pointer.x-button.x)<=16&&std::abs(pointer.y-button.y)<=16;
    button_state=hover?(pointer.down?2:1):0;cached_button=&ui_button_cache[t];
    if(cached_button->valid[button_state]){p=cached_button->paints[button_state];continue;}
   }

   int x0=1280,y0=720,x1=-1,y1=-1;
   auto extend=[&](int x,int y){x0=std::min(x0,x-12);y0=std::min(y0,y-12);x1=std::max(x1,x+12);y1=std::max(y1,y+12);};
   if(pointer_tile)extend(pointer.x,pointer.y);for(auto&b:buttons)extend(b.x,b.y);
   auto inverse=ui_inverses.find(t);if(inverse==ui_inverses.end())inverse=ui_inverses.emplace(t,UiInverse(d)).first;
   // Reuse coordinates already populated by the pointer, but do not warm
   // a new sample grid just to draw a static button.
   auto *samples=(ui_cold_view||(cached_button&&ui_samples.find(t)==ui_samples.end()))?nullptr:&ui_samples[t];
   for(int inv=0;inv<2;inv++){
   auto bounds=mock&&getenv("TRIANGLE_UI_FULL_SCAN")?UiBounds{}:inverse->second.bounds(inv,x0,y0,x1,y1);
   for(int row=bounds.r0;row<=bounds.r1;row++){
    int64_t u=0,v=0,z=0;
    if(!samples){
     u=int32_t(d[6])+int64_t(row)*int32_t(d[7])+int64_t(bounds.c0-1)*int32_t(d[8])+int64_t(inv)*int32_t(d[9]);
     v=int32_t(d[10])+int64_t(row)*int32_t(d[11])+int64_t(bounds.c0-1)*int32_t(d[12])+int64_t(inv)*int32_t(d[13]);
     z=int32_t(d[14])+int64_t(row)*int32_t(d[15])+int64_t(bounds.c0-1)*int32_t(d[16])+int64_t(inv)*int32_t(d[17]);
    }
    for(int c=bounds.c0;c<=std::min(bounds.c1,row-inv);c++){
    unsigned cell=row*row+2*c+inv;
    int x,y;
    if(samples){
     uint32_t sample=samples->get(d,cell,row,c,inv);if(sample==UINT32_MAX)continue;
     bool relevant=pointer_tile&&UiSamples::near(sample,pointer.x,pointer.y,12);
     for(auto&button:buttons)relevant|=UiSamples::near(sample,button.x,button.y,12);if(!relevant)continue;
     x=UiSamples::x(sample);y=UiSamples::y(sample);
    }else{
     u+=int32_t(d[8]);v+=int32_t(d[12]);z+=int32_t(d[16]);
     // A changed output view uses the original sparse calculation once.
     // Filling a new cache's guard cells would delay this first publication.
     if(z<=0||u<0||v<0||u>z*1279||v>z*719)continue;
     auto near=[&](int px,int py){return u>=int64_t(px-12)*z&&u<=int64_t(px+12)*z&&v>=int64_t(py-12)*z&&v<=int64_t(py+12)*z;};
     bool relevant=pointer_tile&&near(pointer.x,pointer.y);for(auto&button:buttons)relevant|=near(button.x,button.y);if(!relevant)continue;
     x=int((u+z/2)/z);y=int((v+z/2)/z);
    }
    int color=-1;
    for(auto&button:buttons){int dx=x-button.x,dy=y-button.y;
     // Mixed edge tiles may contain both app pixels and WM padding. Never
     // paint a button into a sample covered by the application's descriptor.
     auto &window=windows.at(button.window);auto content=window.plan.find(t);
     if(window.has_content&&content!=window.plan.end()){
      auto &cd=content->second.descriptor;std::array<int64_t,3> q{};
      for(unsigned k=0;k<3;k++)q[k]=int32_t(cd[6+4*k])+int64_t(row)*int32_t(cd[7+4*k])+int64_t(c)*int32_t(cd[8+4*k])+inv*int64_t(int32_t(cd[9+4*k]));
      if(covered(cd,q))continue;
     }
     bool hover=std::abs(pointer.x-button.x)<=16&&std::abs(pointer.y-button.y)<=16&&pointer.visible;
     if(hover&&std::abs(dx)<=10&&std::abs(dy)<=10)color=0;
     bool ink=false;
     if(button.kind==1)ink=std::abs(dx)<=7&&std::abs(dy)<=7&&(std::abs(dx-dy)<=1||std::abs(dx+dy)<=1);
     if(button.kind==2)ink=(std::abs(dx)<=1&&std::abs(dy)<=8)||(std::abs(dy)<=1&&std::abs(dx)<=8)||(std::abs(dx)>=5&&std::abs(dx)<=8&&std::abs(std::abs(dy)-(8-std::abs(dx)))<=1)||(std::abs(dy)>=5&&std::abs(dy)<=8&&std::abs(std::abs(dx)-(8-std::abs(dy)))<=1);
     if(button.kind==3)ink=std::abs(dx)<=7&&std::abs(dy)<=7&&(std::abs(dx-dy)<=1||(dx>=4&&dy>=4&&(dx>=6||dy>=6))||(dx<=-4&&dy<=-4&&(dx<=-6||dy<=-6)));
     if(ink)color=hover&&pointer.down?2:1;
    }
    if(pointer_tile){int dx=x-pointer.x,dy=y-pointer.y,dist=dx*dx+dy*dy;
     if(dist<nearest){nearest=dist;near_tile=t;near_cell=cell;}
     if(dist<=20)color=0;
     if(dist<=6){color=pointer.down?2:1;white=true;}
    }
    if(color>=0)p.mask[color][cell/32]|=1u<<(cell%32);
   }}}
   if(cached_button){cached_button->paints[button_state]=p;cached_button->valid[button_state]=true;}
  }
  // A five-screen-pixel dot can fall between coarse triangle-cell centers.
  if(pointer.visible&&pointer.display&&!white&&near_tile<tiles){auto&p=paint[near_tile];uint32_t bit=1u<<(near_cell%32);p.mask[0][near_cell/32]&=~bit;p.mask[pointer.down?2:1][near_cell/32]|=bit;}
  for(auto it=paint.begin();it!=paint.end();){bool any=false;for(auto&m:it->second.mask)for(auto v:m)any|=v!=0;if(!any)it=paint.erase(it);else ++it;}
  for(auto &[t,mask]:preview.masks){auto &p=paint[t];p.mask[3]=mask;for(unsigned i=0;i<32;i++)p.mask[3][i]&=~(p.mask[0][i]|p.mask[1][i]|p.mask[2][i]);}
  // Store final visible colors, including rare pointer/button overlaps.
  for(auto &[t,p]:paint)for(unsigned i=0;i<32;i++){uint32_t above=0;for(int c=3;c>=0;c--){p.mask[c][i]&=~above;above|=p.mask[c][i];}}
  ui_mask_dirty=false;ui_cold_view=false;
  }
  ui_masks_us=microseconds_now()-phase;phase=microseconds_now();
  require(paint.size()<=256,"overlay tile budget");
  // Keep dormant mappings so alternating scene banks can reuse their pixels.
  std::array<bool,256> used{};for(auto&[t,s]:ui_slots)used[s]=true;
  ui_metadata=base;std::vector<bo_batch_entry> ops;
  ui_rebuilt=ui_incremental=ui_reused=ui_written_cells=ui_restored_cells=ui_copy_bytes=0;
  for(auto &[t,p]:paint){
   if(input_checkpoint)input_checkpoint(false);
   if(!ui_slots.count(t)){
    if(ui_slots.size()==256){auto evict=ui_slots.begin();while(evict!=ui_slots.end()&&paint.count(evict->first))++evict;require(evict!=ui_slots.end(),"overlay eviction");used[evict->second]=false;ui_slots.erase(evict);}
    unsigned s=0;while(s<256&&used[s])s++;require(s<256,"overlay slot");ui_slots[t]=s;used[s]=true;
   }
   unsigned slot=ui_slots.at(t),payload=tiles-256+slot,source=base[256+t];
   auto &stamp=ui_stamps[bank][slot];uint64_t version=planned_version[t];
   bool same_base=stamp.tile==t&&stamp.source==source&&stamp.version==version;
   if(same_base&&stamp.color==preview.color&&ui_same_mask(stamp.paint,p)){++ui_reused;ui_metadata[256+t]=payload*1024;continue;}
   auto color=[](unsigned c,unsigned splash){return c==0?0u:c==1?255u:c==2?228u:splash;};
   auto add_blend=[&](const std::array<uint32_t,32>&mask,unsigned mode,unsigned value){
    unsigned cells=0;for(uint32_t v:mask)cells+=__builtin_popcount(v);if(!cells)return;
    bo_batch_entry blend{};blend.kind=BO_BATCH_BLEND;auto&b=blend.op.blend;b.bank=bank;b.tile=payload;b.mode=mode;b.color=value;b.scratch_word=mode?0:header;std::copy(mask.begin(),mask.end(),b.mask);ops.push_back(blend);ui_written_cells+=cells;
   };
   // A small pointer often moves to disjoint cells: restoring it costs more
   // than a DMA base copy. Keep the delta path for entering/leaving outlines.
   auto has_outline=[](const UiPaint&v){return std::any_of(v.mask[3].begin(),v.mask[3].end(),[](uint32_t bits){return bits!=0;});};
   if(!ui_delta||!same_base||(!has_outline(p)&&!has_outline(stamp.paint))){
    ++ui_rebuilt;
    bo_batch_entry op{};
    if(source==UINT32_MAX){op.kind=BO_BATCH_FILL;op.op.fill={BO_SCENE0+bank,(header+payload*256)*4,1024,0,0};}
    else {op.kind=BO_BATCH_COPY;op.op.copy={BO_SCENE0+bank,header*4+source,BO_SCENE0+bank,(header+payload*256)*4,1024,0};}
    ops.push_back(op);ui_copy_bytes+=1024;
    for(unsigned c=0;c<4;c++)add_blend(p.mask[c],1,color(c,preview.color));
   }else{
    ++ui_incremental;std::array<uint32_t,32> restore{};bool removed=false;
    for(unsigned i=0;i<32;i++){uint32_t before=0,after=0;for(unsigned c=0;c<4;c++){before|=stamp.paint.mask[c][i];after|=p.mask[c][i];}restore[i]=before&~after;removed|=restore[i]!=0;ui_restored_cells+=__builtin_popcount(restore[i]);}
    if(removed){
     int solid_color=source==UINT32_MAX?0:-1;for(auto [c,s]:colors)if(source==s*1024)solid_color=c;
     if(solid_color>=0)add_blend(restore,1,unsigned(solid_color));
     else {
      // The ABI reads masked source bytes from scratch. Stage the clean latest
      // tile there; never read back or overwrite the active display bank.
      bo_batch_entry copy{};copy.kind=BO_BATCH_COPY;copy.op.copy={BO_SCENE0+bank,header*4+source,BO_SCRATCH,header*4,1024,0};ops.push_back(copy);ui_copy_bytes+=1024;add_blend(restore,0,0);
     }
    }
    for(unsigned c=0;c<4;c++){
     std::array<uint32_t,32> same{},delta{};
     for(unsigned old=0;old<4;old++)if(color(old,stamp.color)==color(c,preview.color))for(unsigned i=0;i<32;i++)same[i]|=stamp.paint.mask[old][i];
     for(unsigned i=0;i<32;i++)delta[i]=p.mask[c][i]&~same[i];
     add_blend(delta,1,color(c,preview.color));
    }
   }
   stamp.tile=t;stamp.source=source;stamp.version=version;stamp.color=preview.color;stamp.paint=p;
   ui_metadata[256+t]=payload*1024;
  }
  for(size_t i=0;i<ops.size();i+=BO_BATCH_MAX){bo_batch b{};b.count=std::min(size_t(BO_BATCH_MAX),ops.size()-i);b.entries=uintptr_t(ops.data()+i);call(BO_BATCH,&b);}
  ui_payload_us=microseconds_now()-phase;ui_tile_count=paint.size();ui_ms=(now()-start)*1000;ui_dirty=false;
 }
