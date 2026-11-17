"""Check mixed-pose coefficients through WM -> Output -> device, against Python."""
from pathlib import Path
import json,subprocess,sys,tempfile,hashlib,random
here=Path(__file__).resolve().parent.parent/'software/boayo/native'
sys.path[:0]=[str(here/'reference'/p) for p in ('platform','wm','apps')]
from bosio_geometry_v2 import camera_coefficients
binary=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
def call(op,args={},ok=True):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='pose-test'))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert r['ok']==ok,r;return r.get('result')
try:
 with tempfile.TemporaryDirectory() as tmp:
  call('test_resident_digest',dict(path=tmp+'/scene'))
  rng=random.Random(20261114)
  poses=[(7,-3,11),(-8,6,-15),(8,-6,15),(0,85,179),(0,-85,-180),(179.9,84.9,179.9)]
  poses += [(rng.uniform(-179,179),rng.uniform(-84,84),rng.uniform(-179,179)) for _ in range(20)]
  for y,pi,r in poses:
   state=call('view_set',dict(yaw=y,pitch=pi,roll=r))
   trace=call('test_resident_state')['trace'];actual=[e['camera'] for e in trace if e['op']=='pose'][-1]
   expected=camera_coefficients(state['yaw'],state['pitch'],state['roll']).reshape(-1).tolist()
   assert actual==expected,(state,actual,expected)
  state=call('view_set',dict(yaw=370,pitch=100,roll=370));assert state==dict(yaw=10.,pitch=85.,roll=10.)
  assert call('view_move',dict(yaw=-20,pitch=-10,roll=-20))==dict(yaw=-10.,pitch=75.,roll=-10.)
  assert call('view_reset')==dict(yaw=0.,pitch=0.)
  call('view_set',dict(roll='invalid'),False);assert call('view_state')==dict(yaw=0.,pitch=0.)
  # Failed physical submission must not be reflected as a successful WM pose.
  call('test_resident_digest',dict(path=tmp+'/scene',fail_after=0))
  call('view_set',dict(yaw=1,pitch=2,roll=3),False)
  assert call('view_state')==dict(yaw=0.,pitch=0.) and call('test_resident_state')['failed']
finally:p.stdin.close();assert p.wait(timeout=30)==0
report=dict(result='PASS_MIXED_POSE',hardware=False,poses=len(poses),coefficients=len(poses)*180,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),checks=['Python exact coefficients through WM/Output/device','wrap/clamp/reset','invalid and failed device requests leave pose unchanged'])
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
