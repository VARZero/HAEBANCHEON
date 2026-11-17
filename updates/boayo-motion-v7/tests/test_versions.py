from pathlib import Path
import json,os,subprocess,sys,tempfile,base64,hashlib
old,new,assets,plans,out=map(Path,sys.argv[1:6]);out.mkdir(parents=True,exist_ok=True)
def start(exe):return subprocess.Popen([str(exe),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans)))
a,b=start(old),start(new)
def call(p,op,args={}):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');p.stdin.flush();line=p.stdout.readline();assert line,p.stderr.read();r=json.loads(line);assert r['ok'],r;return r['result']
def both(op,args={}):call(a,op,args);return call(b,op,args)
results=[];last=0
try:
 with tempfile.TemporaryDirectory() as t:
  t=Path(t);both('test_desktop',dict(op='tick',now=0));w=call(b,'get_state')['windows'][0]['window_id']
  for i in range(5):
   if i:both('update_surface',dict(window_id=w,x=81,y=72,width=2,height=2,rgb24=base64.b64encode(bytes([i*43,12,73])*4).decode()))
   call(a,'test_render',dict(path=str(t/'old'),full=True));r=call(b,'test_resident_render',dict(path=str(t/'new')))
   assert (t/'old.scene').read_bytes()==(t/'new.scene').read_bytes()
   ops=r['trace'][last:];last=len(r['trace'])
   copied=sum(x['words'] for x in ops if x['op']=='copy')
   uploads=sum(x['words'] for x in ops if x['op']=='upload' and x['region'] in (3,4))
   if i>=2:assert r['projection']['dirty_tiles']<96,r
   if i==2:assert copied>0,'missing generations must be copied'
   if i>=3:assert copied==0,'already matching generations must not be copied'
   if i>=2:assert uploads==0,'unchanged directory already present in both banks'
   if i:assert ops[-1]['command']==2
   results.append(dict(frame=i,dirty=r['projection']['dirty_tiles'],copy_words=copied,metadata_upload_words=uploads))
  for mapped in (False,True):
   both('configure_window',dict(window_id=w,mapped=mapped))
   call(a,'test_render',dict(path=str(t/'old'),full=True));r=call(b,'test_resident_render',dict(path=str(t/'new')))
   assert (t/'old.scene').read_bytes()==(t/'new.scene').read_bytes()
   ops=r['trace'][last:];last=len(r['trace'])
   assert not any(x['op'] in ('run','fill','copy','tile_blend') for x in ops),ops
   assert ops[-1]['command']==1
   results.append(dict(frame='show_reuse' if mapped else 'hide',projection=r['projection']))
finally:
 for p in (a,b):p.stdin.close();assert p.wait(timeout=30)==0,p.stderr.read()
report=dict(result='PASS_LAZY_VERSIONS',frames=results,candidate_sha256=hashlib.sha256(new.read_bytes()).hexdigest(),hardware=False)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
