"""Differential test against the frozen Python runtime, never against a rewritten oracle."""
import sys,json,subprocess,base64,random,tempfile,hashlib
from pathlib import Path
import numpy as np
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('platform','wm','apps')]
from boayo_kfb_daemon import KFBDaemon
from bosio_geometry_v2 import camera_coefficients
from projection_model import prepare_window,reference_job
binary=sys.argv[1]
proc=subprocess.Popen([binary,'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
oracle=KFBDaemon('/tmp/unused-boayo-comparison',headless=True,m=8)
count=0
def request(op,args=None,owner='test'):
 global count
 r=dict(op=op,args=args or {},app=owner,id=count);count+=1
 proc.stdin.write(json.dumps(r)+'\n');proc.stdin.flush()
 line=proc.stdout.readline();assert line,'native process died'
 return json.loads(line)
def close(a,b,path=''):
 if isinstance(a,dict):
  assert isinstance(b,dict) and a.keys()==b.keys(),(path,a,b)
  for k in a:close(a[k],b[k],path+'/'+k)
 elif isinstance(a,list):
  assert isinstance(b,list) and len(a)==len(b),(path,a,b)
  for i,(x,y) in enumerate(zip(a,b)):close(x,y,path+'/'+str(i))
 elif isinstance(a,float):assert abs(a-b)<2e-6,(path,a,b)
 else:assert a==b,(path,a,b)
def compare(op,args=None,owner='test'):
 r=dict(op=op,args=args or {},app=owner)
 try:expected=oracle.dispatch(r);ok=True
 except Exception as e:expected=str(e);ok=False
 actual=request(op,args,owner)
 assert actual['ok']==ok,(r,expected,actual)
 if ok:
  if op=='get_state':
   expected={k:expected[k] for k in ('generation','focused_window','pointer','windows')}
   actual['result'].pop('compositor',None)
  close(expected,actual['result'],op)
 return expected
random.seed(7331)
for i in range(20):
 compare('create_window',dict(title=f'Window {i}',azimuth=(i-10)*13,elevation=i%5*9-18,surface_width=48,surface_height=32,always_on_top=i%7==0,decorated=i%3==0,workspace={'role':'app'}),f'app{i%4}')
for i in range(250):
 wid=random.randrange(1,21);owner=f'app{(wid-1)%4}'
 op=random.choice(['configure_window','focus_window','raise_window','lower_window','fill','pointer_warp','input_click','input_scroll','input_key','view_move'])
 a={'window_id':wid}
 if op=='configure_window':a.update(azimuth=random.uniform(-400,400),elevation=random.uniform(-100,100),mapped=random.choice([True,True,False]))
 elif op=='fill':a['rgb']=[random.randrange(256) for _ in range(3)]
 elif op=='pointer_warp':a=dict(azimuth=random.uniform(-180,180),elevation=random.uniform(-89,89))
 elif op=='input_scroll':a={'delta':random.choice([-1,1])}
 elif op=='input_key':a={'key':random.choice(['next','tab','enter','escape'])}
 elif op=='view_move':a={'yaw':random.uniform(-400,400),'pitch':random.uniform(-100,100)}
 compare(op,a,owner);compare('get_state')
 for app in range(4):compare('poll_events',owner=f'app{app}')
compare('create_window',dict(title='capture',azimuth=0,elevation=0,surface_width=16,surface_height=16,always_on_top=True))
compare('pointer_warp',dict(azimuth=0,elevation=0))
compare('input_button',dict(button='left',pressed=True))
compare('capture_pointer',dict(window_id=21))
compare('pointer_warp',dict(azimuth=75,elevation=34))
compare('input_button',dict(button='left',pressed=False));compare('get_state');compare('poll_events')
for op,a in [('update_surface',dict(window_id=21,x=2,y=3,width=4,height=2,rgb24=base64.b64encode(bytes(range(24))).decode())),('fill',dict(window_id=21,rgb=[3,256,0])),('configure_window',dict(window_id=21,shape=[[0,0,17,4,0]])),('destroy_window',dict(window_id=999))]:compare(op,a)
assert base64.b64decode(request('test_surface',{'window_id':21})['result'])==oracle.manager.windows[21].surface.tobytes()
for yaw,pitch,roll in [(0,0,0),(2,2,0),(-178,85,0),(37,-29,13)]:
 got=np.array(request('test_camera',dict(yaw=yaw,pitch=pitch,roll=roll))['result'],dtype=np.int32)
 expected=camera_coefficients(yaw,pitch,roll).reshape(-1);np.testing.assert_array_equal(got,expected)
 with tempfile.TemporaryDirectory() as tmp:
  path=str(Path(tmp)/'descriptors')
  assert request('test_descriptors',dict(path=path,yaw=yaw,pitch=pitch,roll=roll))['ok']
  d,v=prepare_window(640,360,yaw=yaw,pitch=pitch,roll=roll,fov_h=42,fov_v=30)
  np.testing.assert_array_equal(np.fromfile(path+'.bin',dtype='<u4').reshape(-1,18),d)
  np.testing.assert_array_equal(np.fromfile(path+'.visible',dtype=np.uint8),v)
texture=np.random.default_rng(7331).integers(0,1<<24,16*12,dtype=np.uint32)
desc,visible=prepare_window(16,12,fov_h=70,fov_v=50)
for d in desc[np.flatnonzero(visible)[::7]]:
 d=d.copy();d[1]=0;scene=np.zeros(256,dtype=np.uint32);cells=reference_job(texture,scene,d)
 got=request('test_project',dict(descriptor=d.tolist(),texture=texture.tolist(),scene=[0]*256))
 assert got['ok'],got;assert got['result']['cells']==cells;np.testing.assert_array_equal(got['result']['scene'],scene)
proc.stdin.close();assert proc.wait()==0
print(json.dumps(dict(result='PASS',requests=count,geometry='exact camera and 16880 descriptors',projection='bit-exact integer model',wm='state, events, capture, shape, surface and invalid inputs')))
