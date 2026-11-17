#pragma once
#include "wm.hpp"
namespace boayo {
// Cache only the geometric hit test, never input/button state or window data.
// Exact ordered keys avoid hash collisions and invalidate on all hit() inputs.
struct SceneStateCache {
  J previous;Hit hit;uint64_t queries=0,recomputes=0;
  J state(const WM &wm){
    J key=J::array({wm.paz,wm.pel});
    for(int id:wm.order){const auto &d=wm.windows.at(id).data;
      key.push_back(J::array({id,d.at("mapped"),d.at("input_enabled"),
        d.at("azimuth"),d.at("elevation"),d.at("roll"),d.at("width_deg"),d.at("height_deg"),
        d.at("surface_width"),d.at("surface_height"),d.at("shape"),d.at("decorated")}));
    }
    ++queries;
    if(key!=previous){hit=wm.hit(wm.paz,wm.pel);previous=std::move(key);++recomputes;}
    J windows=J::array();for(int id:wm.order)windows.push_back(wm.public_window(id));
    // Same public schema as WM::state(); regression checks both after mutations.
    return {{"generation",wm.generation},{"compositor","cpp-native"},
      {"focused_window",wm.focus?J(wm.focus):J(nullptr)},{"pointer",wm.pointer_state(&hit)},{"windows",std::move(windows)}};
  }
};
}
