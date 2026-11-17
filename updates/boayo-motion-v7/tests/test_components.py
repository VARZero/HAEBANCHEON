"""Exact pixels and final device scene against the preserved v5 CPU path."""
from pathlib import Path
import json,os,subprocess,sys,tempfile,random,hashlib,struct
old,new,assets,plans,out=map(Path,sys.argv[1:6]);out.mkdir(parents=True,exist_ok=True)
class RPC:
 def __init__(self,binary,fast=True):
  env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans),BOAYO_INIT_TRACE='0')
  if not fast:env.update(BOAYO_ASSETS='0',BOAYO_PLANS='0')
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,'RPC closed';v=json.loads(line);assert v['ok'],v;return v['result']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=30)==0,self.p.stderr.read()
checks=[]
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);a=RPC(old,False);b=RPC(new)
 try:
  rng=random.Random(163)
  cases=[]
  for kind in ('dashboard','telemetry','sdk-pulse'):
   for i in range(40):
    state=dict(focused=bool(i%2),hovered=['','move','resize_right','close'][i%4],step=rng.randrange(10000) if kind=='telemetry' else i%10,
      apps=i,stored=i//3,clicks=i*29,width_deg=38,height_deg=28)
    cases.append((kind,state))
  # Fast recipe and functional fallback at nonstandard geometry/counts.
  cases += [('dashboard',dict(apps=12345678)),('sdk-pulse',dict(width_deg=45,height_deg=31)),('telemetry',dict(width=480,height=320,step=120))]
  for i,(kind,state) in enumerate(cases):
   for p,name in [(a,'old'),(b,'new')]:p.call('test_ui',dict(kind='app',app=kind,state=state,path=str(tmp/name)))
   assert (tmp/'old').read_bytes()==(tmp/'new').read_bytes(),('app pixels',i,kind,state)
  checks.append(dict(name='exact_app_pixels',cases=len(cases)))
 finally:a.close();b.close()
 a=RPC(old,False);b=RPC(new)
 try:
  def both(op,args={}):
   x=a.call(op,args);y=b.call(op,args);return y
  def frame(name,precomputed=True):
   a.call('test_render',dict(path=str(tmp/'old'),full=True))
   v=b.call('test_resident_render',dict(path=str(tmp/'new')))
   assert (tmp/'old.scene').read_bytes()==(tmp/'new.scene').read_bytes(),('scene',name)
   m=v['projection']
   if precomputed:assert m['geometry_tiles_evaluated_total']==m['mask_samples_evaluated_total']==0,(name,m)
   assert m['cpu_scene_words']==m['cpu_result_download_words']==0
   assert all(t.get('words')==256 for t in v['trace'] if t['op']=='fill')
   assert not any(t['op']=='download' for t in v['trace'])
   if name in ('dashboard_and_controls','three_windows','close_telemetry_reveal'):
    data=(tmp/'new.scene').read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    (out/(name+'.hex')).write_text(''.join(f'{w:08x}\n' for w in words))
   checks.append(dict(name=name,scene_sha256=hashlib.sha256((tmp/'new.scene').read_bytes()).hexdigest(),projection=m))
  both('test_desktop',dict(op='tick',now=0));frame('launcher')
  dash=both('test_desktop',dict(op='launch',kind='dashboard'));frame('dashboard_and_controls')
  both('test_desktop',dict(op='tick',now=1));frame('dashboard_tick')
  tele=both('test_desktop',dict(op='launch',kind='telemetry',azimuth=12));frame('telemetry_overlap')
  both('test_desktop',dict(op='tick',now=2));frame('telemetry_tick')
  pulse=both('test_desktop',dict(op='launch',kind='sdk-pulse',azimuth=-12));frame('three_windows')
  both('test_desktop',dict(op='tick',now=3));frame('three_windows_tick')
  both('test_desktop',dict(op='close',window_id=pulse));frame('close_pulse_reveal')
  both('test_desktop',dict(op='close',window_id=tele));frame('close_telemetry_reveal')
  both('focus_window',dict(window_id=dash));both('test_desktop',dict(op='tick',now=4));frame('dashboard_refocus')
  # Check viewport changes separately: pointer geometry remains a dynamic path.
  both('view_move',dict(yaw=6,pitch=4));frame('camera_angle',False)
  both('view_reset');frame('camera_reset',False)
  both('test_desktop',dict(op='close',window_id=dash));frame('close_all',False)
  stats=b.call('test_asset_stats');checks.append(dict(name='asset_metrics',stats=stats))
 finally:a.close();b.close()
(out/'result.json').write_text(json.dumps(dict(result='PASS',hardware=False,candidate_sha256=hashlib.sha256(new.read_bytes()).hexdigest(),baseline_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),checks=checks),indent=2)+'\n')
print('PASS_COMPONENTS',len(checks),'groups: exact pixels, layered scenes, controls, overlap, close/reveal, camera')
