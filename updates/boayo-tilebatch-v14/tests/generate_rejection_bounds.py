from pathlib import Path
import re,argparse
p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args()
src=Path(__file__).resolve().parents[1]/'software/boayo/native/src'
# Independent spatial bounds: affine upper endpoints and the inversion vector.
text=(src/'fixed_geometry.inc').read_text().split('fixed_triangles')[1]
vs=[tuple(float.fromhex(x) for x in a.split(',')) for a in re.findall(r'V\{([^}]+)\}',text)];assert len(vs)==4220*3
tri=[vs[i:i+3] for i in range(0,len(vs),3)]
rows=[]
for size in (32,4):
 bounds=[]
 for i in range(0,len(tri),size):
  ends=[];invs=[]
  for q in tri[i:i+size]:
   for j in range(3):ends.append(tuple(94*q[j][k]+q[(j+1)%3][k]+q[(j+2)%3][k] for k in range(3)))
   invs.append(tuple(q[0][k]-2*q[1][k]+q[2][k] for k in range(3)))
  vals=[min(v[k] for v in ends) for k in range(3)]+[max(v[k] for v in ends) for k in range(3)]+[min(v[k] for v in invs) for k in range(3)]+[max(v[k] for v in invs) for k in range(3)]
  bounds.append('  {{'+','.join(x.hex() for x in vals)+'}},')
 rows.append('static constexpr std::array<std::array<double,12>,'+str(len(bounds))+'> rejection_bounds'+str(size)+' = {{\n'+'\n'.join(bounds)+'\n}};\n')
generated='// Generated conservative affine endpoint and inversion bounds.\n'+''.join(rows)
target=src/'rejection_bounds.inc'
if args.check:
 assert target.read_bytes()==generated.encode(), 'generated bounds differ'
 print('PASS_REJECTION_BOUNDS_REPRODUCIBLE')
else:
 target.write_bytes(generated.encode())
