#pragma once
struct UiControl {int window=0,kind=0,x=0,y=0;unsigned tile=0;}; // 1 close, 2 move, 3 resize
struct UiPaint {std::array<std::array<uint32_t,32>,4> mask{};};
static bool ui_same_mask(const UiPaint&a,const UiPaint&b){
 uint32_t difference=0;
 for(unsigned c=0;c<4;c++)for(unsigned i=0;i<32;i++)difference|=a.mask[c][i]^b.mask[c][i];
 return difference==0;
}
struct UiStamp {unsigned tile=~0u,source=~0u,color=228;uint64_t version=0;UiPaint paint;};
struct UiEdgeMask {std::array<uint32_t,3> exposed{};std::array<uint32_t,32> mask{};};
struct UiPointer {int x=640,y=360;bool visible=true,down=false,display=true;};
struct UiButtonCache {std::array<UiPaint,3> paints{};std::array<bool,3> valid{};};

struct UiPreview {int window=0;unsigned color=228;double yaw=0,pitch=0,width=0,height=0;std::vector<unsigned> footprint;std::map<unsigned,std::array<uint32_t,32>> masks;};
struct MoveMarker {
 bool active=false,frozen=false,committing=false,drawn=false,needs_publish=false;int window=0,kind=0;
 unsigned tile=~0u,color=228;Words base,scratch;
 std::array<unsigned,2> bank_tiles{{~0u,~0u}};
 std::vector<unsigned> rim,last_rim;std::array<std::vector<unsigned>,2> bank_rims;
 WindowTransform resize_transform;uint64_t rim_updates=0,resize_prepare_us=0;
 double target_yaw=0,target_pitch=0,target_width=0,target_height=0;
 std::array<uint8_t,4220> occluded{};
 uint64_t frames=0,freeze_copies=0,payload_builds=0,payload_reuses=0;
 unsigned last_tile=~0u,changed_entries=0,last_payload_ops=0;
};
