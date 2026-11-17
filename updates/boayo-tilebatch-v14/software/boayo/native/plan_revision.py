from pathlib import Path
import hashlib,json
s=Path(__file__).resolve().parent/'src'
h=hashlib.sha256()
for name in ('geometry.cpp','geometry.hpp','fixed_geometry.inc','common.hpp','wm.cpp','backend.cpp','backend.hpp','mask.hpp','culling.hpp','culling_bounds.inc','rejection_bounds.inc','geometry_math.hpp','geometry_reference.inc','plan.cpp','plan.hpp'):
 h.update(name.encode()+b'\0'+(s/name).read_bytes())
revision=h.hexdigest()
header='#pragma once\n#define BOAYO_PLAN_REV "'+revision+'"\n'
compat=s.parent/'plan_compatibility.json'
if compat.exists():
 proof=json.loads(compat.read_text())
 if proof['source_revision']==revision:
  assert proof['verified_plans']==37 and proof['result']=='PASS_EXACT_PAYLOADS'
  header+='#define BOAYO_PLAN_COMPAT_REV "'+proof['payload_revision']+'"\n'
(s/'plan_revision.hpp').write_bytes(header.encode())
