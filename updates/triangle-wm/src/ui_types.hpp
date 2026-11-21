#pragma once
struct UiControl {int window=0,kind=0,x=0,y=0;unsigned tile=0;}; // 1 close, 2 move, 3 resize
struct UiPaint {std::array<std::array<uint32_t,32>,3> mask{};};
struct UiStamp {unsigned tile=~0u,source=~0u;uint64_t version=0,hash=0;};
struct UiPointer {int x=640,y=360;bool visible=true,down=false;};
