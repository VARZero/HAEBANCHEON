"""Full scene pixel comparison for all 25 motion stages against preserved v5."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
old,new,assets,plans,out=map(Path,sys.argv[1:6]);out.mkdir(parents=True,exist_ok=True)
class RPC:
 def __init__(self,binary,fast):
  env=dict(os.environ,BOAYO_INIT_TRACE='0',BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans))
  if not fast:env.update(BOAYO_ASSETS='0',BOAYO_PLANS='0')
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=env)
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='motion-pixel-test'))+'\n');self.p.stdin.flush()
  r=json.loads(self.p.stdout.readline());assert r['ok'],r;return r['result']
 def close(self):self.p.stdin.close();assert self.p.wait(timeout=30)==0
ref=RPC(old,False);dut=RPC(new,True);checks=[];now=0;yaw=pitch=0
try:
 with tempfile.TemporaryDirectory() as temp:
  temp=Path(temp)
  def both(op,a={}):
   x=ref.call(op,a);y=dut.call(op,a);return y
  def tick():both('test_desktop',dict(op='tick',now=now))
  def state():return dut.call('get_state')
  def focus(index):both('focus_window',dict(window_id=ids[index]));tick()
  def control(index,name,da=0,de=0):
   focus(index)
   w=next(w for w in state()['windows'] if w['workspace'].get('parent')==ids[index] and w['workspace'].get('control')==name)
   az,el=w['azimuth'],w['elevation']
   both('input_warp',dict(azimuth=az,elevation=el));both('input_button',dict(button='left',pressed=True));tick()
   if name!='close':both('input_warp',dict(azimuth=az+da,elevation=el+de));tick()
   both('input_button',dict(button='left',pressed=False));tick()
  def pose(y,p,r):
   global yaw,pitch
   ref.call('view_move',dict(yaw=y-yaw,pitch=p-pitch))
   dut.call('view_set',dict(yaw=y,pitch=p,roll=r));yaw,pitch=y,p
  def frame(n):
   ref.call('test_render',dict(path=str(temp/'ref')))
   dut.call('test_resident_render',dict(path=str(temp/'dut')))
   a=(temp/'ref.scene').read_bytes();b=(temp/'dut.scene').read_bytes()
   assert a==b,('scene mismatch',n,next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),None))
   checks.append(dict(stage=n,scene_sha256=hashlib.sha256(b).hexdigest()))
  tick();ids=[both('test_desktop',dict(op='launch',kind=kind)) for kind in ('dashboard','telemetry','sdk-pulse')]
  tick();frame(-1)
  poses=[(0,0,0),(-12,0,0),(12,0,0),(0,-8,0),(0,8,0),(0,0,-20),(0,0,20),(-8,6,-15),(8,-6,15),(0,0,0)]
  for n in range(25):
   now=n+1
   if n<10:pose(*poses[n])
   elif n<=12:control(0,'move',20 if n==11 else -10,-8 if n==11 else 4)
   elif n<=14:control(0,'resize_right',6 if n==13 else -6,-4 if n==13 else 4)
   elif n==15:control(1,'move',4,2)
   elif n<=17:focus(0 if n==16 else 1)
   elif n<=19:control(2,'move',-12 if n==18 else 12,4 if n==18 else -4)
   elif n==20:pose(6,4,15)
   elif n==21:pose(0,0,0)
   else:control(24-n,'close')
   tick();frame(n)
finally:ref.close();dut.close()
result=dict(result='PASS_MOTION_PIXELS',hardware=False,candidate_sha256=hashlib.sha256(new.read_bytes()).hexdigest(),baseline_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),checks=checks)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS_MOTION_PIXELS 26 full scenes, 25 motion stages; camera coefficient comparison is separate')
