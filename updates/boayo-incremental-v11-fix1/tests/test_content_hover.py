import json,os,subprocess,sys,tempfile
from pathlib import Path
old,new,assets=sys.argv[1:]
class RPC:
 def __init__(self,binary,enabled):
  self.p=subprocess.Popen([binary,'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=assets,BOAYO_ASSETS=str(enabled)))
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args))+'\n');self.p.stdin.flush()
  r=json.loads(self.p.stdout.readline());assert r['ok'],r;return r['result']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=30)==0
ref,original,fixed=RPC(old,0),RPC(old,1),RPC(new,1)
try:
 with tempfile.TemporaryDirectory() as td:
  root=Path(td);checks=0
  # Reproduce the physical fallback: the frame is visually neutral for content.
  original.call('test_ui',dict(kind='app',app='dashboard',state={'hovered':'content'},path=str(root/'old')))
  before=original.call('test_asset_stats');assert before['misses']>=2,before
  for app,title,w,h in [('dashboard','Dashboard',640,440),('telemetry','Telemetry',400,300),('sdk-pulse','SDK Pulse',400,300)]:
   for focused in (False,True):
    for hover in ('','content','outside','move','resize_right','close'):
     for kind in ('app','frame'):
      args=dict(kind=kind,app=app,title=title,width=w,height=h,focused=focused,hovered=hover,external=True,state=dict(width=w,height=h,focused=focused,hovered=hover))
      for name,p in [('reference',ref),('fixed',fixed)]:p.call('test_ui',dict(args,path=str(root/name)))
      assert (root/'reference').read_bytes()==(root/'fixed').read_bytes(),(app,focused,hover,kind)
      checks+=1
  after=fixed.call('test_asset_stats');assert after['misses']==0 and after['rejected']==0,after
  print(json.dumps(dict(result='PASS',pixel_comparisons=checks,original_content_fallback=before,fixed_assets=after)))
finally:
 for p in (ref,original,fixed):p.close()
