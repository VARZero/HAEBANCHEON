 void set_ui_view(double y,double p){
  if(view_yaw==y&&view_pitch==p)return;
  view_yaw=y;view_pitch=p;view_axes=axes(y,p);ui_projection.reset();ui_transform.reset();ui_descriptors.clear();ui_inverses.clear();ui_samples.clear();ui_cold_view=true;ui_dirty=true;ui_layout_dirty=true;ui_mask_dirty=true;
 }
 V ui_ray(int x,int y)const{
  return view_axes[0]+view_axes[1]*((x-639.5)*(2*std::tan(rad(24.))/1280))+
    view_axes[2]*((359.5-y)*(2*std::tan(rad(18.))/720));
 }
 unsigned ui_pick(int x,int y)const{
  auto ray=ui_ray(x,y);int64_t q[3];for(int k=0;k<3;k++)q[k]=std::llround(ray[k]*0x1p30);
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
  for(int id:order){auto&w=windows.at(id);if(!w.visible)continue;
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
  double start=now();if(ui_layout_dirty){ui_layout();ui_layout_dirty=false;ui_mask_dirty=true;}auto &paint=ui_paints;
  if(ui_mask_dirty){paint.clear();
  for(auto&c:ui_controls)paint[c.tile];
  std::set<unsigned> cursor;
  if(pointer.visible&&pointer.display){unsigned t=ui_pick(pointer.x,pointer.y);if(t<tiles)cursor.insert(t);
   for(int round=0;round<2;round++){auto previous=cursor;for(unsigned v:previous)for(auto&e:tile_neighbors[v]){cursor.insert(e[0]);if(e[1]!=65535)cursor.insert(e[1]);}}
   for(unsigned v:cursor)paint[v];
  }
  int nearest=INT_MAX;unsigned near_tile=tiles,near_cell=0;bool white=false;
  for(auto &[t,p]:paint){auto&d=ui_descriptor(t);
   bool pointer_tile=pointer.visible&&pointer.display&&cursor.count(t)&&affected(d,{pointer.x-12,pointer.y-12,25,25});
   std::vector<UiControl> buttons;for(auto&c:ui_controls)if(c.tile==t)buttons.push_back(c);
   if(!pointer_tile&&buttons.empty())continue;
   int x0=1280,y0=720,x1=-1,y1=-1;
   auto extend=[&](int x,int y){x0=std::min(x0,x-12);y0=std::min(y0,y-12);x1=std::max(x1,x+12);y1=std::max(y1,y+12);};
   if(pointer_tile)extend(pointer.x,pointer.y);for(auto&b:buttons)extend(b.x,b.y);
   auto inverse=ui_inverses.find(t);if(inverse==ui_inverses.end())inverse=ui_inverses.emplace(t,UiInverse(d)).first;
   auto *samples=ui_cold_view?nullptr:&ui_samples[t];
   for(int inv=0;inv<2;inv++){
   auto bounds=mock&&getenv("TRIANGLE_UI_FULL_SCAN")?UiBounds{}:inverse->second.bounds(inv,x0,y0,x1,y1);
   for(int row=bounds.r0;row<=bounds.r1;row++)for(int c=bounds.c0;c<=std::min(bounds.c1,row-inv);c++){
    unsigned cell=row*row+2*c+inv;
    int x,y;
    if(samples){
     uint32_t sample=samples->get(d,cell,row,c,inv);if(sample==UINT32_MAX)continue;
     bool relevant=pointer_tile&&UiSamples::near(sample,pointer.x,pointer.y,12);
     for(auto&button:buttons)relevant|=UiSamples::near(sample,button.x,button.y,12);if(!relevant)continue;
     x=UiSamples::x(sample);y=UiSamples::y(sample);
    }else{
     // A changed output view uses the original sparse calculation once.
     // Filling a new cache's guard cells would delay this first publication.
     int64_t q[3];for(unsigned k=0;k<3;k++)q[k]=int32_t(d[6+4*k])+int64_t(row)*int32_t(d[7+4*k])+int64_t(c)*int32_t(d[8+4*k])+int64_t(inv)*int32_t(d[9+4*k]);
     auto u=q[0],v=q[1],z=q[2];if(z<=0||u<0||v<0||u>z*1279||v>z*719)continue;
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
   }}
  }
  // A five-screen-pixel dot can fall between coarse triangle-cell centers.
  if(pointer.visible&&pointer.display&&!white&&near_tile<tiles){auto&p=paint[near_tile];uint32_t bit=1u<<(near_cell%32);p.mask[0][near_cell/32]&=~bit;p.mask[pointer.down?2:1][near_cell/32]|=bit;}
  for(auto it=paint.begin();it!=paint.end();){bool any=false;for(auto&m:it->second.mask)for(auto v:m)any|=v!=0;if(!any)it=paint.erase(it);else ++it;}
  ui_mask_dirty=false;ui_cold_view=false;
  }
  require(paint.size()<=256,"overlay tile budget");
  for(auto it=ui_slots.begin();it!=ui_slots.end();)if(!paint.count(it->first))it=ui_slots.erase(it);else ++it;
  std::array<bool,256> used{};for(auto&[t,s]:ui_slots)used[s]=true;
  ui_metadata=base;std::vector<bo_batch_entry> ops;
  for(auto &[t,p]:paint){
   if(!ui_slots.count(t)){unsigned s=0;while(used[s])s++;ui_slots[t]=s;used[s]=true;}
   unsigned slot=ui_slots.at(t),payload=tiles-256+slot,source=base[256+t];uint64_t hash=1469598103934665603ull;
   for(auto&m:p.mask)for(auto v:m)hash=(hash^v)*1099511628211ull;
   auto &stamp=ui_stamps[bank][slot];uint64_t version=planned_version[t];
   if(stamp.tile!=t||stamp.source!=source||stamp.version!=version||stamp.hash!=hash){
    bo_batch_entry op{};
    if(source==UINT32_MAX){op.kind=BO_BATCH_FILL;op.op.fill={BO_SCENE0+bank,(header+payload*256)*4,1024,0,0};}
    else {op.kind=BO_BATCH_COPY;op.op.copy={BO_SCENE0+bank,header*4+source,BO_SCENE0+bank,(header+payload*256)*4,1024,0};}
    ops.push_back(op);
    for(unsigned c=0;c<3;c++){bool any=false;for(auto v:p.mask[c])any|=v!=0;if(!any)continue;
     bo_batch_entry blend{};blend.kind=BO_BATCH_BLEND;blend.op.blend.bank=bank;blend.op.blend.tile=payload;blend.op.blend.mode=1;blend.op.blend.color=c==0?0:c==1?255:228;std::copy(p.mask[c].begin(),p.mask[c].end(),blend.op.blend.mask);ops.push_back(blend);
    }
    stamp={t,source,version,hash};
   }
   ui_metadata[256+t]=payload*1024;
  }
  for(size_t i=0;i<ops.size();i+=BO_BATCH_MAX){bo_batch b{};b.count=std::min(size_t(BO_BATCH_MAX),ops.size()-i);b.entries=uintptr_t(ops.data()+i);call(BO_BATCH,&b);}
  ui_tile_count=paint.size();ui_ms=(now()-start)*1000;ui_dirty=false;
 }
