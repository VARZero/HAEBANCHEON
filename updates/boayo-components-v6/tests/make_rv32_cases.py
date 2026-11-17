from pathlib import Path
import json,os,subprocess,sys,tempfile
binary,assets,plans,out=map(Path,sys.argv[1:5]);out.mkdir(parents=True,exist_ok=True)
requests=[]
def add(op,args={}):requests.append(dict(op=op,args=args,app='boayo-desktop'))
def frame():add('test_resident_digest',dict(path='/tmp/components-scene'))
add('test_desktop',dict(op='tick',now=0));frame()
add('test_desktop',dict(op='launch',kind='dashboard'));frame()
add('test_desktop',dict(op='tick',now=1));frame()
add('test_desktop',dict(op='launch',kind='telemetry',azimuth=12));frame()
add('test_desktop',dict(op='tick',now=2));frame()
add('test_desktop',dict(op='launch',kind='sdk-pulse',azimuth=-12));frame()
add('test_desktop',dict(op='tick',now=3));frame()
# IDs are derived and checked from actual host replies before generating close cases.
p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,
 env=dict(os.environ,BOAYO_INIT_TRACE='0',BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans)))
replies=[];ids=[]
try:
 def invoke(request):
  p.stdin.write(json.dumps(request)+'\n');p.stdin.flush();reply=json.loads(p.stdout.readline());assert reply['ok'],reply
  if request['op']=='test_resident_digest':
   m=reply['result']['projection'];assert m['geometry_tiles_evaluated_total']==m['mask_samples_evaluated_total']==0
   assert reply['result']['assets']['rgb_materializations']==0,reply
  replies.append(reply)
 for q in requests:
  invoke(q)
  if q['op']=='test_desktop' and q['args']['op']=='launch':ids.append(replies[-1]['result'])
 for id in reversed(ids):
  add('test_desktop',dict(op='close',window_id=id));invoke(requests[-1]);frame();invoke(requests[-1])
 p.stdin.close();assert p.wait(timeout=30)==0
finally:
 if p.poll() is None:p.kill();p.wait()
(out/'requests.json').write_text(json.dumps(requests,indent=2)+'\n')
(out/'expected.json').write_text(json.dumps(replies,indent=2)+'\n')
print('PASS_HOST_RV32_ORACLE',len(requests),'requests',sum(q['op']=='test_resident_digest' for q in requests),'exact scene digests')
