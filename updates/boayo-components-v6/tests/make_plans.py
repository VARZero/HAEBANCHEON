"""Compile device descriptors/masks for a declared app/component placement catalog."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys,tempfile,zlib
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from ready_plan import device_plan
binary=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
catalog_path=Path(sys.argv[3]) if len(sys.argv)>3 else Path(__file__).resolve().parent.parent/'asset_catalog.json'
catalog=json.loads(catalog_path.read_text())
manifest=dict(format='BOGPLN02',plans=[],catalog=catalog,generator_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
seen={}
def session():
 return subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_PLANS='0',BOAYO_ASSETS='0',BOAYO_INIT_TRACE='0'))
def call(p,op,args={}):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');p.stdin.flush()
 reply=json.loads(p.stdout.readline());assert reply['ok'],reply;return reply['result']
def export(p,w,raw):
 info=call(p,'test_plan_export',dict(window_id=w['window_id'],path=str(raw)))
 if info['key'] in seen:return seen[info['key']]
 body=raw.read_bytes();key=info['key'].encode();assert len(key)<=4096-28 and len(body)==info['bytes']
 h=bytearray(4096);struct.pack_into('<8sIIII',h,0,b'BOGPLN02',info['count'],len(body),len(key),zlib.crc32(body))
 struct.pack_into('<I',h,24,zlib.crc32(h[:24]+key));h[28:28+len(key)]=key
 name_hash=2166136261
 for b in key:name_hash=((name_hash^b)*16777619)&0xffffffff
 name=f'p_{name_hash:08x}.bgp';data=h+body
 assert not any(x['file']==name for x in manifest['plans']),'plan hash collision'
 (out/name).write_bytes(data)
 info.update(file=name,sha256=hashlib.sha256(data).hexdigest(),file_bytes=len(data),title=w['title'])
 # General multi-layer plans may exceed the first-scene queue's 128-job limit.
 if info['count']-info['empty']<=128:
  ready=device_plan(data);(out/(name+'.ready')).write_bytes(ready)
  info.update(ready_file=name+'.ready',ready_sha256=hashlib.sha256(ready).hexdigest())
 manifest['plans'].append(info);seen[info['key']]=name;return name
with tempfile.TemporaryDirectory() as tmp:
 for entry in [dict(kind='launcher',placements=[[0,0]])]+catalog['applications']:
  for az,el in entry['placements']:
   p=session()
   try:
    call(p,'test_desktop',dict(op='tick',now=0))
    if entry['kind']!='launcher':call(p,'test_desktop',dict(op='launch',kind=entry['kind'],azimuth=az,elevation=el))
    for w in call(p,'get_state')['windows']:
     if w['mapped']:
      name=export(p,w,Path(tmp)/'body')
      if entry['kind']=='launcher':manifest['bootstrap']=name
    p.stdin.close();assert p.wait(timeout=30)==0
   finally:
    if p.poll() is None:p.kill();p.wait()
manifest['total_bytes']=sum(x['file_bytes'] for x in manifest['plans'])
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(result='PASS_GENERATED_COMPONENT_PLANS',plans=len(manifest['plans']),bytes=manifest['total_bytes'])))
