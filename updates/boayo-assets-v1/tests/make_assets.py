"""Render exact existing UI on the host and encode lossless RGB word runs."""
from pathlib import Path
import hashlib,itertools,json,os,struct,subprocess,sys,tempfile,zlib
binary=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
apps=[dict(id='dashboard',name='Dashboard',color='#347CFF'),dict(id='telemetry',name='Telemetry',color='#33B57A'),dict(id='sdk-pulse',name='SDK Pulse',color='#347CFF')]
jobs=[]
for selected,hovered,offset in itertools.product(range(-1,3),range(-1,3),range(3)):
 a=dict(kind='launcher',apps=apps,selected=selected,hovered=hovered,offset=offset)
 jobs.append((a,['launcher',apps,selected,hovered,offset,True,False,640,360]))
for name,selected,h in itertools.product(('move','resize_right','close'),(False,True),(40,80)):
 jobs.append((dict(kind='caption',name=name,selected=selected,height=h),['caption',name,selected,h]))
for title,w,h in [('Dashboard',640,440),('Telemetry',400,300),('SDK Pulse',400,300)]:
 for focused,hovered,external in itertools.product((False,True),('','move','resize_right','close'),(False,True)):
  jobs.append((dict(kind='frame',title=title,width=w,height=h,focused=focused,hovered=hovered,external=external),['frame',title,w,h,focused,hovered,external]))
 for selected,dim,closing in itertools.product((False,True),repeat=3):
  jobs.append((dict(kind='storage',title=title,selected=selected,dim=dim,closing=closing),['storage',title,selected,dim,closing]))
def key_bytes(key):return json.dumps(key,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
def filename(key):
 h=2166136261
 for b in key:h=((h^b)*16777619)&0xffffffff
 return f'{h:08x}.bui'
manifest=dict(format='BUIRLE01',generator_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),assets=[])
proc=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSETS='0',BOAYO_INIT_TRACE='0'))
try:
 with tempfile.TemporaryDirectory() as tmp:
  rawpath=Path(tmp)/'pixels'
  for args,key in jobs:
   proc.stdin.write(json.dumps(dict(op='test_ui',args=dict(args,path=str(rawpath))))+'\n');proc.stdin.flush()
   reply=json.loads(proc.stdout.readline());assert reply['ok'],reply
   raw=rawpath.read_bytes();w=reply['result']['width'];h=reply['result']['height'];assert len(raw)==w*h*3
   runs=[];previous=None;count=0
   for i in range(0,len(raw),3):
    color=(raw[i]<<16)|(raw[i+1]<<8)|raw[i+2]
    if color==previous:count+=1
    else:
     if count:runs.append((count,previous))
     previous=color;count=1
   runs.append((count,previous));kb=key_bytes(key)
   body=kb+b''.join(struct.pack('<II',*v) for v in runs)
   prefix=b'BUIRLE01'+struct.pack('<IIII',w,h,len(kb),len(runs))
   data=prefix+struct.pack('<I',zlib.crc32(prefix+body))+body;name=filename(kb)
   p=out/name
   if p.exists():assert p.read_bytes()==data,'key collision or stale asset'
   p.write_bytes(data)
   manifest['assets'].append(dict(file=name,key=key,args=args,width=w,height=h,bytes=len(data),runs=len(runs),sha256=hashlib.sha256(data).hexdigest(),pixels_sha256=hashlib.sha256(raw).hexdigest()))
  proc.stdin.close();assert proc.wait(timeout=30)==0
finally:
 if proc.poll() is None:proc.kill();proc.wait()
default=['launcher',apps,-1,-1,0,True,False,640,360]
manifest['bootstrap']=filename(key_bytes(default));manifest['total_bytes']=sum(x['bytes'] for x in manifest['assets'])
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(result='PASS_GENERATED',assets=len(jobs),bytes=manifest['total_bytes'],bootstrap=manifest['bootstrap'])))
