"""Regenerate conservative 32-tile bounds from the checked-in fixed geometry."""
from pathlib import Path
import re
src=Path(__file__).resolve().parents[1]/'software/boayo/native/src'
text=(src/'fixed_geometry.inc').read_text().split('fixed_triangles')[1]
vertices=[tuple(float.fromhex(x.strip()) for x in a.split(',')) for a in re.findall(r'V\{([^}]+)\}',text)]
assert len(vertices)==4220*3 and max(abs(x) for v in vertices for x in v)<=1.000001
rows=[]
for i in range(0,len(vertices),96):
 block=vertices[i:i+96]
 vals=[min(v[j] for v in block) for j in range(3)]+[max(v[j] for v in block) for j in range(3)]
 rows.append('  {{'+','.join(x.hex() for x in vals)+'}},')
(src/'culling_bounds.inc').write_text('// Bounds of each consecutive 32 fixed triangles, generated from exact double literals.\nstatic constexpr std::array<std::array<double,6>,132> tile_group_bounds = {{\n'+'\n'.join(rows)+'\n}};\n')
print('GENERATED_132_GROUPS; rerun culling and plan equivalence tests after regeneration')
