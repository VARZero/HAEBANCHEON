"""Rebuild all shipped payloads with the new masks; authorize only this source hash."""
from pathlib import Path
import json,os,subprocess,sys,tempfile,hashlib
binary,plans,native,out=map(Path,sys.argv[1:])
m=json.loads((plans/'manifest.json').read_text());assert len(m['plans'])==37
p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_PLANS='0',BOAYO_ASSETS='0'))
def call(op,args):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='mask-proof'))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert r['ok'],r;return r['result']
checks=[];revision=None;old_revision=None
try:
 with tempfile.TemporaryDirectory() as td:
  path=Path(td)/'body'
  for entry in m['plans']:
   old=json.loads(entry['key']);g=old['geometry'];old_revision=old['revision']
   w=call('create_window',dict(title='mask-proof',surface_width=g[0],surface_height=g[1],azimuth=g[2],elevation=g[3],roll=g[4],width_deg=g[5],height_deg=g[6],shape=old['shape'],decorated=False))['window_id']
   call('configure_window',dict(window_id=w,roll=g[4]))
   info=call('test_plan_export',dict(window_id=w,path=str(path)))
   payload=(plans/entry['file']).read_bytes()[4096:]
   assert path.read_bytes()==payload,entry['file']
   revision=json.loads(info['key'])['revision'];checks.append(dict(file=entry['file'],payload_sha256=hashlib.sha256(payload).hexdigest()))
   call('destroy_window',dict(window_id=w))
 p.stdin.close();assert p.wait(timeout=30)==0
finally:
 if p.poll() is None:p.kill();p.wait()
result=dict(result='PASS_EXACT_PAYLOADS',source_revision=revision,payload_revision=old_revision,verified_plans=len(checks),checks=checks)
out.write_text(json.dumps(result,indent=2)+'\n')
(native/'plan_compatibility.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='checks'}))
