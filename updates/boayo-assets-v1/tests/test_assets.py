from pathlib import Path
import base64,hashlib,json,os,subprocess,sys,tempfile,shutil,struct,zlib
baseline=Path(sys.argv[1]);candidate=Path(sys.argv[2]);assets=Path(sys.argv[3]);out=Path(sys.argv[4]);out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((assets/'manifest.json').read_text())
class RPC:
 def __init__(self,binary,directory,trace=False):
  self.log=out/('rpc-'+str(len(list(out.glob('rpc-*.log'))))+'.log');self.f=self.log.open('w')
  env=dict(os.environ,BOAYO_ASSET_DIR=str(directory),BOAYO_INIT_TRACE=str(int(trace)),BOAYO_INIT_TRACE_UART='0',BOAYO_TEST_RGB_DMA='1')
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.f,text=True,env=env)
 def call(self,op,args={},owner='boayo-desktop'):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app=owner))+'\n');self.p.stdin.flush()
  reply=json.loads(self.p.stdout.readline());assert reply['ok'],reply;return reply['result']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=30)==0;self.f.close()
checks=[]
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp);base=RPC(baseline,root/'missing');fast=RPC(candidate,assets)
 try:
  for i,a in enumerate(manifest['assets']):
   for proc,name in ((base,'base'),(fast,'fast')):
    proc.call('test_ui',dict(a['args'],path=str(root/name)))
   expected=(root/'base').read_bytes();actual=(root/'fast').read_bytes()
   assert actual==expected and hashlib.sha256(actual).hexdigest()==a['pixels_sha256'],a['args']
  stats=fast.call('test_asset_stats');assert stats['loads']==132 and not stats['rejected'],stats
  checks.append('132 precomputed UI variants bit-exact against prior executable')
 finally:base.close();fast.close()
 def sequence(binary,directory,label):
  p=RPC(binary,directory,True);scenes=[];metrics=[]
  try:
   p.call('test_desktop',dict(op='tick',now=0))
   win=p.call('get_state')['windows'][0]['window_id']
   def frame():
    m=p.call('test_render',dict(path=str(root/'frame'),full=True));scenes.append(hashlib.sha256((root/'frame.scene').read_bytes()).hexdigest());metrics.append(m['projection'])
   frame()
   p.call('configure_window',dict(window_id=win,azimuth=12));frame()
   p.call('configure_window',dict(window_id=win,azimuth=-5));frame()
   p.call('update_surface',dict(window_id=win,x=81,y=72,width=2,height=2,rgb24=base64.b64encode(bytes([211,12,73])*4).decode()));frame()
   p.call('fill',dict(window_id=win,rgb=[31,127,251]));frame()
   p.call('test_desktop',dict(op='show_launcher',azimuth=0,elevation=0,now=1));frame()
   before=p.call('test_surface',dict(window_id=win))
   p.call('pointer_warp',dict(azimuth=6.5,elevation=7.5))
   p.call('test_desktop',dict(op='tick',now=1.1));frame()
   assert p.call('test_surface',dict(window_id=win))!=before,'hover did not select an item'
   p.call('test_desktop',dict(op='show_launcher',azimuth=0,elevation=0,now=1.2));frame()
   p.call('test_desktop',dict(op='show_launcher',azimuth=0,elevation=0,now=1.3));frame()
   return scenes,metrics,p.log
  finally:p.close()
 b,bm,bl=sequence(baseline,root/'missing','base')
 c,cm,cl=sequence(candidate,assets,'fast')
 assert b==c,'prepared texture/mutation/geometry restore mismatch'
 assert cm[1]['texture_upload_words']==0 and cm[2]['texture_upload_words']==0,cm
 assert cm[2]['texture_reuses_total']>=2 and cm[3]['texture_upload_words']>0
 log=cl.read_text();assert 'event=asset.loaded' in log and 'event=desktop.end' in log
 assert 'event=rounded.begin' not in log and 'event=circle.begin' not in log
 checks.append('full scenes exact after movement, partial edit, fill, restore, hover, repeated publication; resident reuse and mutation invalidation exercised')
 checks.append('first desktop uses asset with no rounded/circle CPU drawing')
 default=next(a for a in manifest['assets'] if a['file']==manifest['bootstrap']);gold=(assets/default['file']).read_bytes()
 for case in ('missing','truncated','crc','dimensions','run_overflow'):
  d=root/case;d.mkdir();bad=bytearray(gold)
  if case=='truncated':bad=bad[:-1]
  if case=='crc':bad[-1]^=1
  if case=='dimensions':struct.pack_into('<I',bad,8,0)
  if case=='run_overflow':
   k=struct.unpack_from('<I',bad,16)[0];struct.pack_into('<I',bad,28+k,0xffffffff)
   struct.pack_into('<I',bad,24,zlib.crc32(bad[:24]+bad[28:]))
  if case!='missing':(d/default['file']).write_bytes(bad)
  p=RPC(candidate,d)
  try:
   p.call('test_ui',dict(default['args'],path=str(root/'fallback')))
   assert hashlib.sha256((root/'fallback').read_bytes()).hexdigest()==default['pixels_sha256']
   stats=p.call('test_asset_stats');assert stats['loads']==0 and stats['misses']>0
   if case!='missing':assert stats['rejected']==1
  finally:p.close()
 checks.append('missing/truncated/CRC/header/overflow asset safely falls back with identical pixels')
report=dict(result='PASS_ASSETS_AND_PROJECTED_SCENES',checks=checks,variants=132,hardware=False,baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
