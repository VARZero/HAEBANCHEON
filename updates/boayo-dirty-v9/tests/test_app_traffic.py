"""Compare real Dashboard/Telemetry updates; traffic is Mock, not wall time."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
old,new,assets,plans,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
class RPC:
 def __init__(self,binary):
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans)))
  self.cursor=0
 def call(self,op,args):
  self.p.stdin.write(json.dumps(dict(app='traffic-test',op=op,args=args))+'\n');self.p.stdin.flush()
  r=json.loads(self.p.stdout.readline());assert r['ok'],r;return r['result']
 def close(self):self.p.stdin.close();assert self.p.wait(timeout=30)==0
a,b=RPC(old),RPC(new);rows=[]
try:
 with tempfile.TemporaryDirectory() as tmp:
  for rpc in (a,b):
   rpc.call('test_desktop',dict(op='tick',now=0))
   rpc.call('test_desktop',dict(op='launch',kind='dashboard',azimuth=-22))
   rpc.call('test_desktop',dict(op='launch',kind='telemetry',azimuth=22))
  for n in range(13):
   row=dict(frame=n)
   for label,rpc in [('old',a),('new',b)]:
    rpc.call('test_desktop',dict(op='tick',now=n))
    result=rpc.call('test_resident_render',dict(path=tmp+'/'+label))
    trace=result['trace'][rpc.cursor:];rpc.cursor=len(result['trace'])
    row[label]=dict(direct_bytes=sum(x['bytes'] for x in trace if x['op']=='direct_upload'),jobs=sum(result['projection']['jobs_per_core']),redrawn=result['projection']['redrawn_tiles'])
    if label=='new':row['pool']=result['projection']['texture_pool']
   assert Path(tmp+'/old.scene').read_bytes()==Path(tmp+'/new.scene').read_bytes(),n
   rows.append(row)
finally:a.close();b.close()
old_bytes=sum(r['old']['direct_bytes'] for r in rows[1:]);new_bytes=sum(r['new']['direct_bytes'] for r in rows[1:])
assert new_bytes<old_bytes,(old_bytes,new_bytes)
report=dict(result='PASS_APP_TRAFFIC',hardware=False,scope='12 updates after first composition',old_direct_bytes=old_bytes,new_direct_bytes=new_bytes,reduction_percent=(1-new_bytes/old_bytes)*100,frames=rows,baseline_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256(new.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS_APP_TRAFFIC',old_bytes,'->',new_bytes,'bytes, reduction',report['reduction_percent'],'percent; 13 scene comparisons')
