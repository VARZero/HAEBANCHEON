"""Host-only exact descriptor and mask generation for the default launcher."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys,tempfile,zlib
binary=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_PLANS='0',BOAYO_ASSETS='0',BOAYO_INIT_TRACE='0'))
def call(op,args={}):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');p.stdin.flush()
 reply=json.loads(p.stdout.readline());assert reply['ok'],reply;return reply['result']
try:
 with tempfile.TemporaryDirectory() as tmp:
  call('test_desktop',dict(op='tick',now=0));window=call('get_state')['windows'][0]['window_id']
  raw=Path(tmp)/'plan';info=call('test_plan_export',dict(window_id=window,path=str(raw)))
  body=raw.read_bytes();key=info['key'].encode();assert len(key)<=4096-28 and len(body)==info['bytes']
  h=bytearray(4096);struct.pack_into('<8sIIII',h,0,b'BOGPLN01',info['count'],len(body),len(key),zlib.crc32(body))
  struct.pack_into('<I',h,24,zlib.crc32(h[:24]+key));h[28:28+len(key)]=key
  name_hash=2166136261
  for b in key:name_hash=((name_hash^b)*16777619)&0xffffffff
  name=f'p_{name_hash:08x}.bgp';data=h+body
  for old in out.glob('p_*.bgp'):old.unlink()
  (out/name).write_bytes(data)
  info.update(file=name,sha256=hashlib.sha256(data).hexdigest(),file_bytes=len(data))
  manifest=dict(format='BOGPLN01',plans=[info],bootstrap=name,generator_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
  (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
  print(json.dumps(dict(result='PASS_GENERATED_PLAN',file=name,tiles=info['count'],full=info['full'],empty=info['empty'],boundary=info['boundary'],bytes=len(data))))
 p.stdin.close();assert p.wait(timeout=30)==0
finally:
 if p.poll() is None:p.kill();p.wait()
