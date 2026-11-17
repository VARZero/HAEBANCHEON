from pathlib import Path
import base64,hashlib,json,os,struct,subprocess,sys,tempfile,zlib
baseline,candidate,assets,plans,out=map(Path,sys.argv[1:6]);out.mkdir(parents=True,exist_ok=True)
rv32='--rv32' in sys.argv
manifest=json.loads((plans/'manifest.json').read_text());entry=manifest['plans'][0];checks=[]
class RPC:
 def __init__(self,binary,directory,enabled=True):
  self.log=out/('rpc-'+str(len(list(out.glob('rpc-*.log'))))+'.log');self.f=self.log.open('w')
  command=(["qemu-riscv32","-cpu","rv32,c=false,f=false,d=false"] if rv32 else [])+[str(binary),'--rpc']
  self.p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.f,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(directory),BOAYO_PLANS='1' if enabled else '0',BOAYO_INIT_TRACE='1',BOAYO_INIT_TRACE_UART='0'))
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line, self.log.read_text();r=json.loads(line);assert r['ok'],r;return r['result']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=90)==0;self.f.close()
with tempfile.TemporaryDirectory() as td:
 root=Path(td)
 def sequence(binary,directory):
  p=RPC(binary,directory);scenes=[];metrics=[]
  try:
   p.call('test_desktop',dict(op='tick',now=0));win=p.call('get_state')['windows'][0]['window_id']
   def frame():
    m=p.call('test_render',dict(path=str(root/'frame'),full=True));scenes.append(hashlib.sha256((root/'frame.scene').read_bytes()).hexdigest());metrics.append(m['projection']);return m
   frame()
   p.call('update_surface',dict(window_id=win,x=81,y=72,width=2,height=2,rgb24=base64.b64encode(bytes([211,12,73])*4).decode()));frame()
   p.call('configure_window',dict(window_id=win,azimuth=12));frame()
   p.call('configure_window',dict(window_id=win,azimuth=0));frame()
   p.call('configure_window',dict(window_id=win,shape=[]));frame()
   p.call('configure_window',dict(window_id=win,shape=entry['shape']));frame()
   p.call('configure_window',dict(window_id=win,roll=15,width_deg=50));frame()
   p.call('configure_window',dict(window_id=win,roll=0,width_deg=42));frame()
   front=p.call('create_window',dict(title='overlay',surface_width=64,surface_height=64,width_deg=18,height_deg=14,decorated=False,always_on_top=True))['window_id']
   p.call('fill',dict(window_id=front,rgb=[0,64,255]));frame()
   p.call('destroy_window',dict(window_id=front));frame()
   return scenes,metrics,p.log
  finally:p.close()
 b,bm,bl=sequence(baseline,root/'missing');c,cm,cl=sequence(candidate,plans)
 assert b==c,'cached descriptors/masks changed final scene'
 assert cm[0]['geometry_tiles_evaluated_total']==0 and cm[0]['mask_samples_evaluated_total']==0 and cm[0]['precomputed_layers_total']==1,cm[0]
 assert sum(cm[0]['jobs_per_core'])==entry['count']-entry['empty']
 assert sum(cm[0]['jobs_per_core'])<sum(bm[0]['jobs_per_core'])
 assert cm[1]['geometry_tiles_evaluated_total']==0 and cm[1]['mask_samples_evaluated_total']==0
 assert cm[2]['geometry_tiles_evaluated_total']>0 and cm[2]['mask_samples_evaluated_total']>0
 assert cm[3]['geometry_tiles_evaluated_total']==cm[2]['geometry_tiles_evaluated_total'] and cm[3]['mask_samples_evaluated_total']==cm[2]['mask_samples_evaluated_total']
 assert sum(cm[4]['jobs_per_core'])>0,'revealed clipped tiles were not reprojected'
 assert cm[5]['mask_samples_evaluated_total']==cm[4]['mask_samples_evaluated_total']
 log=cl.read_text();assert 'event=plan.load.end' in log and 'event=texture.upload.end' in log
 assert 'event=geometry.compute.begin' not in log and 'event=mask.compute.begin' not in log
 checks.append('10 scenes exact: initial, pixel edit, move/restore, reveal clipped tiles/restore shape, roll/FOV/restore, occlusion/removal')
 checks.append('first frame performs zero descriptor candidates and zero mask sample calculations; clipped jobs omitted')
 # Independent execution of uncached generator, including RV32 libm when requested.
 p=RPC(candidate,plans,False)
 try:
  p.call('test_desktop',dict(op='tick',now=0));win=p.call('get_state')['windows'][0]['window_id']
  raw=root/'computed';info=p.call('test_plan_export',dict(window_id=win,path=str(raw)))
  gold=(plans/entry['file']).read_bytes()
  assert raw.read_bytes()==gold[4096:] and info['key']==entry['key'],'host/RV32 descriptor or mask differs'
 finally:p.close()
 checks.append('uncached calculation exactly matches every prebuilt descriptor/mask byte')
 def fix(data):
  struct.pack_into('<I',data,20,zlib.crc32(data[4096:]));k=struct.unpack_from('<I',data,16)[0]
  struct.pack_into('<I',data,24,zlib.crc32(data[:24]+data[28:28+k]))
 for case in ('missing','truncated','crc','revision','tile_order','descriptor','mask_offset','mask_value'):
  directory=root/case;directory.mkdir();bad=bytearray(gold)
  if case=='truncated':bad=bad[:-1]
  elif case=='crc':bad[-1]^=1
  elif case=='revision':
   k=struct.unpack_from('<I',bad,16)[0];key=bytes(bad[28:28+k]);at=key.index(b'revision')+len('revision')+3;bad[28+at]^=1;fix(bad)
  elif case=='tile_order':struct.pack_into('<I',bad,4096+84,struct.unpack_from('<I',bad,4096)[0]);fix(bad)
  elif case=='descriptor':struct.pack_into('<I',bad,4096+12,0xffffffff);fix(bad)
  elif case in ('mask_offset','mask_value'):
   n=struct.unpack_from('<I',bad,8)[0];i=next(i for i in range(n) if struct.unpack_from('<I',bad,4096+i*84+4)[0]==2)
   at=4096+i*84+8;off=struct.unpack_from('<I',bad,at)[0]
   if case=='mask_offset':struct.pack_into('<I',bad,at,0)
   else:bad[4096+off]=2
   fix(bad)
  if case!='missing':(directory/entry['file']).write_bytes(bad)
  p=RPC(candidate,directory)
  try:
   p.call('test_desktop',dict(op='tick',now=0));m=p.call('test_render',dict(path=str(root/'bad'),full=True))
   assert hashlib.sha256((root/'bad.scene').read_bytes()).hexdigest()==b[0],case
   assert m['projection']['geometry_tiles_evaluated_total']>0 and m['projection']['mask_samples_evaluated_total']>0,case
   if case!='missing':assert m['projection']['plan_cache']['rejected']>0,case
  finally:p.close()
 checks.append('missing/stale/truncated/CRC/bounds/invalid-mask plans fall back to identical output')
report=dict(result='PASS_PRECOMPUTED_PLANS',checks=checks,execution='QEMU RV32 userspace' if rv32 else 'host',hardware=False,first_frame=cm[0],baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
