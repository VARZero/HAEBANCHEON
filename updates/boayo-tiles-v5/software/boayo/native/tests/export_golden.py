from pathlib import Path
import sys,struct,json
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference/wm'),str(here/'reference/apps')]
from bosio_geometry_v2 import camera_coefficients
from projection_model import tile_descriptor
from boayo_caption import button_image
def fnv(data):
 value=2166136261
 for x in data:value=((value^x)*16777619)&0xffffffff
 return value
camera=[fnv(camera_coefficients(*p).astype('<i4').tobytes()) for p in [(0,0,0),(37,-29,13)]]
desc=[]
for t in [0,211,1234,4219]:
 d,v=tile_descriptor(t//211,t%211,640,360,yaw=37,pitch=-29,roll=13,fov_h=42,fov_v=30,dst=t*256)
 desc.append(fnv(d.astype('<u4').tobytes()))
caption=[fnv(button_image(name,True,100 if name=='move' else 40).tobytes()) for name in ('move','resize_right','close')]
(here/'src/golden.hpp').write_text('#pragma once\n#include <cstdint>\nnamespace boayo {\n'+''.join('inline constexpr uint32_t golden_'+name+'[]={'+','.join(str(v)+'u' for v in values)+'};\n' for name,values in [('camera',camera),('descriptor',desc),('caption',caption)])+'}\n')
print(json.dumps(dict(camera=camera,descriptor=desc,caption=caption)))
