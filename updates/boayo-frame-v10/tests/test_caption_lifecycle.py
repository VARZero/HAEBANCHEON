"""Compare exact state and composed pixels across cached-caption invalidation."""
import os,json,subprocess,tempfile,sys,hashlib
from pathlib import Path
old,new,assets,plans,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
class RPC:
 def __init__(self,p):
  self.p=subprocess.Popen([str(p),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans),BOAYO_INIT_TRACE='0'))
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='caption-lifecycle'))+'\n');self.p.stdin.flush();r=json.loads(self.p.stdout.readline());assert r['ok'],r;return r['result']
 def close(self):self.p.stdin.close();assert self.p.wait(timeout=30)==0
a,b=RPC(old),RPC(new);checks=[]
try:
 with tempfile.TemporaryDirectory() as td:
  def both(op,args={}):
   x=a.call(op,args);y=b.call(op,args);assert x==y,(op,x,y);return y
  def tick(t):both('test_desktop',dict(op='tick',now=t))
  def frame(name):
   both('get_state')
   for name2,rpc in [('old',a),('new',b)]:rpc.call('test_resident_render',dict(path=td+'/'+name2))
   x=Path(td+'/old.scene').read_bytes();y=Path(td+'/new.scene').read_bytes();assert x==y,name
   checks.append(dict(name=name,sha256=hashlib.sha256(y).hexdigest()))
  tick(0);wid=both('test_desktop',dict(op='launch',kind='telemetry'));tick(1);frame('initial')
  for t in (2,3,4):tick(t);frame('unchanged_layout_'+str(t))
  both('test_desktop',dict(op='minimize',window_id=wid,now=5));tick(6);frame('minimized')
  both('test_desktop',dict(op='restore',window_id=wid,now=7));tick(8);frame('restore_same_geometry')
  both('test_desktop',dict(op='minimize',window_id=wid,now=9));tick(10);frame('minimized_again')
  both('test_desktop',dict(op='restore',window_id=wid,now=11,azimuth=20,elevation=10));tick(12);frame('restore_new_geometry')
  other=both('test_desktop',dict(op='launch',kind='dashboard'));tick(13);frame('focus_other')
  both('focus_window',dict(window_id=wid));tick(14);frame('focus_restored')
  state=both('get_state');cap=next(w for w in state['windows'] if w['workspace'].get('parent')==wid and w['workspace'].get('control')=='close')
  both('input_warp',dict(azimuth=cap['azimuth'],elevation=cap['elevation']));tick(15);frame('hover_close')
  both('input_warp',dict(azimuth=-60,elevation=50));tick(16);frame('leave_close')
  both('test_desktop',dict(op='close',window_id=wid,now=17));tick(18);frame('close')
finally:a.close();b.close()
report=dict(result='PASS_CAPTION_LIFECYCLE',hardware=False,checks=checks)
(out/'result.json').write_text(json.dumps(report,indent=2));print(report['result'],len(checks),'exact state/scene comparisons')
