import sys,json,subprocess,dataclasses,random
from pathlib import Path
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('platform','wm','apps')]
from boayo_kfb_daemon import KFBDaemon
from bosio_wm_client import BosioWMClient
import boayo_app_window as aw
from boayo_spatial_workspace import storage_layout
class DirectClient(BosioWMClient):
 def __init__(self,daemon):self.daemon=daemon
 def call(self,op,**args):return self.daemon.dispatch(dict(op=op,args=args,app='test'))
daemon=KFBDaemon('/tmp/unused-app-diff',headless=True,m=8)
client=DirectClient(daemon);clock=[0.];aw.time.monotonic=lambda:clock[0]
proc=subprocess.Popen([sys.argv[1],'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
count=0
def request(op,args,expected_error=False):
 global count
 count+=1;proc.stdin.write(json.dumps(dict(op=op,args=args,app='test'))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['ok']!=expected_error,r;return r.get('result',r.get('error'))
def same(a,b,path=''):
 if isinstance(a,dict):
  assert a.keys()==b.keys(),(path,a,b)
  for k in a:same(a[k],b[k],path+'/'+k)
 elif isinstance(a,(list,tuple)):
  assert len(a)==len(b),(path,a,b)
  for i,(x,y) in enumerate(zip(a,b)):same(x,y,path+'/'+str(i))
 elif isinstance(a,float):assert abs(a-b)<1e-7,(path,a,b)
 else:assert a==b,(path,a,b)
app=aw.BoayoApplicationWindow(client,'SDK Pulse',0,0,38,28)
same(dataclasses.asdict(app.state),request('test_app',dict(op='create')))
def check(op,**args):
 now=args.pop('now',clock[0]);clock[0]=now
 if op=='tick':app.tick(now)
 elif op=='minimize':app.minimize()
 elif op=='restore':app.restore(args.get('azimuth'),args.get('elevation'))
 elif op=='close':app.close()
 elif op=='event':app.handle_event(args['event'])
 same(dataclasses.asdict(app.state),request('test_app',dict(op=op,now=now,**args)),op)
 expected=daemon.manager.state();actual=request('get_state',{})
 expected.pop('compositor');actual.pop('compositor');same(expected,actual,op+'/wm')
def event(kind,**args):check('event',event=dict(type=kind,window_id=app.window_id,**args))
check('tick',now=1)
event('key',key='tab',pressed=True)
event('key',key='tab',pressed=True)
event('pointer_button',button='left',pressed=True,u=.02,v=.5,azimuth=-20,elevation=0)
event('pointer_motion',u=.02,v=.5,azimuth=5,elevation=12)
event('pointer_button',button='left',pressed=False,u=.02,v=.5,azimuth=5,elevation=12)
check('minimize',now=5);check('tick',now=5.1);check('tick',now=5.23);check('restore',azimuth=198,elevation=-100)
event('pointer_button',button='left',pressed=True,u=.95,v=.93,azimuth=-160,elevation=-70)
event('pointer_motion',u=.95,v=.93,azimuth=-150,elevation=-75)
event('pointer_cancel')
check('minimize',now=8);check('tick',now=8.23);check('restore')
event('focus',focused=False);event('pointer_leave');check('close')
random.seed(95)
for size in (1,2,8,32,64):
 windows=[dict(window_id=i+1,azimuth=random.uniform(-180,180),workspace={'dock_azimuth':random.choice([179.,-179.,0.,20.])}) for i in range(size)]
 try:expected={str(k):v for k,v in storage_layout(windows).items()}
 except ValueError as exc:assert request('test_storage_layout',dict(windows=windows),True)==str(exc)
 else:same(expected,request('test_storage_layout',dict(windows=windows)))
proc.stdin.close();assert proc.wait()==0
print(json.dumps(dict(result='PASS',requests=count,coverage='caption placement, move, resize, minimize, restore, cancellation, close, storage seam/collisions')))
