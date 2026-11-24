// Independent original fixed-point flat scan, retained for differential testing.
 std::vector<unsigned> flat_reference(Engine&e,Window&w){

  w.transform.update(w.yaw,w.pitch,w.width,w.height,w.padding);const auto &ax=w.transform.ax;Fx tx=w.transform.padded_x,ty=w.transform.padded_y;
  std::array<FxV,5> normal{ax[0],ax[0]*tx+ax[1],ax[0]*tx-ax[1],ax[0]*ty+ax[2],ax[0]*ty-ax[2]};int32_t plane[5][3];
  for(int p=0;p<5;p++){Fx largest=0;for(Fx v:normal[p])largest=std::max(largest,fixed::abs(v));for(int k=0;k<3;k++)plane[p][k]=int32_t(fixed::lround(normal[p][k]/largest*8192));}
  std::vector<unsigned> result;
  // Conservative plane rejection keeps whole edge tiles; no cell masks.
  for(unsigned group=0;group<132;group++){bool keep=true;for(int p=0;p<5&&keep;p++){int32_t upper=0;for(int k=0;k<3;k++)upper+=plane[p][k]*group_bounds[group][k+(plane[p][k]>=0?3:0)];keep=upper>=-32768;}if(!keep)continue;
   for(unsigned t=group*32;t<std::min(tiles,(group+1)*32);t++){bool hit=true;for(int p=0;p<5&&hit;p++){int32_t upper=INT32_MIN;for(int v=0;v<3;v++){int32_t d=0;for(int k=0;k<3;k++)d+=plane[p][k]*tile_vertex[t][v][k];upper=std::max(upper,d);}hit=upper>=-32768;}if(hit)result.push_back(t);}
  }
  if(e.trim_background){
   // Only tip candidates need the unpadded test; avoid a second full mesh scan.
   std::array<FxV,5> content_normal{ax[0],ax[0]*(w.transform.tx/2)+ax[1],ax[0]*(w.transform.tx/2)-ax[1],ax[0]*(w.transform.ty/2)+ax[2],ax[0]*(w.transform.ty/2)-ax[2]};
   int32_t content_plane[5][3];
   for(int p=0;p<5;p++){Fx largest=0;for(Fx v:content_normal[p])largest=std::max(largest,fixed::abs(v));for(int k=0;k<3;k++)content_plane[p][k]=int32_t(fixed::lround(content_normal[p][k]/largest*8192));}
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
