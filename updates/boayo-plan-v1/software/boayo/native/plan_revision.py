from pathlib import Path
import hashlib
s=Path(__file__).resolve().parent/'src'
h=hashlib.sha256()
for name in ('geometry.cpp','geometry.hpp','fixed_geometry.inc','common.hpp','wm.cpp','backend.cpp','backend.hpp','plan.cpp','plan.hpp'):
 h.update(name.encode()+b'\0'+(s/name).read_bytes())
(s/'plan_revision.hpp').write_bytes(('#pragma once\n#define BOAYO_PLAN_REV "'+h.hexdigest()+'"\n').encode())
