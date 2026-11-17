from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys,tempfile
binary,assets,plans,out=map(Path,sys.argv[1:5]);out.mkdir(parents=True,exist_ok=True)
entry=json.loads((plans/'manifest.json').read_text())['plans'][0]
checks=[]
class RPC:
 def __init__(self,directory,enabled=True):
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,
   env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(directory),BOAYO_READY_PLANS='1' if enabled else '0'))
 def reply(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,'RPC exited';return json.loads(line)
 def call(self,op,args={}):
  r=self.reply(op,args);assert r['ok'],r;return r['result']
 def start(self):
  self.call('test_desktop',dict(op='tick',now=0));return self.call('get_state')['windows'][0]['window_id']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=30)==0
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp)
 p=RPC(plans)
 try:
  w=p.start();p.call('test_device_plan_export',dict(window_id=w,path=str(tmp/'export')))
  assert (tmp/'export').read_bytes()==(plans/entry['ready_file']).read_bytes(),'C++ and independent Python recipe disagree'
  good=p.call('test_resident_render',dict(path=str(tmp/'good')))
  assert good['projection']['device_ready_frames']==1
  reference=(tmp/'good.scene').read_bytes()
 finally:p.close()
 checks.append('independent C++/Python device commands exactly equal')
 for enabled in (False,True):
  p=RPC(plans,enabled)
  try:
   p.start();r=p.call('test_resident_render',dict(path=str(tmp/'candidate')))
   assert (tmp/'candidate.scene').read_bytes()==reference
   assert r['projection']['device_ready_frames']==int(enabled)
  finally:p.close()
 checks.append('prepared and generic execution paths produce identical first scene')
 original=(plans/entry['ready_file']).read_bytes()
 for kind in ('missing','truncated','offset','descriptor','batch','directory'):
  d=tmp/kind;d.mkdir();shutil.copy2(plans/entry['file'],d/entry['file'])
  data=bytearray(original)
  if kind=='truncated':data=data[:48]
  elif kind=='offset':struct.pack_into('<I',data,8*4,0xfffffff0)
  elif kind in ('descriptor','batch','directory'):
   index={'descriptor':8,'batch':12,'directory':16}[kind]
   offset=struct.unpack_from('<I',data,index*4)[0]*4;data[offset]^=1
  if kind!='missing':(d/entry['ready_file']).write_bytes(data)
  p=RPC(d)
  try:
   p.start();r=p.call('test_resident_render',dict(path=str(tmp/'candidate')))
   assert r['projection']['device_ready_frames']==0
   assert (tmp/'candidate.scene').read_bytes()==reference,kind
  finally:p.close()
 checks.append('missing/truncated/bad offsets/descriptors/batch/directory safely fall back to identical scene')
 for after in (0,43,44,47,50,80,90):
  p=RPC(plans)
  try:
   p.start();r=p.reply('test_resident_render',dict(path=str(tmp/'failed'),fail_after=after));assert not r['ok'],(after,r)
   state=p.call('test_resident_state');assert state['failed'] and state['commits']==1,(after,state)
   n=len(state['trace']);r=p.reply('test_resident_render',dict(path=str(tmp/'retry')));assert not r['ok']
   assert len(p.call('test_resident_state')['trace'])==n,'failed scene retried device writes'
  finally:p.close()
 checks.append('7 injected failures: no incomplete presentation; retries blocked without further writes')
report=dict(result='PASS_DEVICE_READY',checks=checks,candidate_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),hardware=False)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
