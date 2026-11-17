from pathlib import Path
import os,sys,subprocess,json,tempfile,hashlib,base64
baseline,candidate,assets,plans,out=map(Path,sys.argv[1:6]);out.mkdir(parents=True,exist_ok=True)
class RPC:
 def __init__(self,binary):
  self.p=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_PLAN_DIR=str(plans)))
 def call(self,op,args={}):
  self.p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');self.p.stdin.flush()
  line=self.p.stdout.readline();assert line,self.p.stderr.read();r=json.loads(line);assert r['ok'],r;return r['result']
 def close(self):
  self.p.stdin.close();assert self.p.wait(timeout=30)==0,self.p.stderr.read()
results=[]
trace_count=0
with tempfile.TemporaryDirectory() as temp:
 a,b=RPC(baseline),RPC(candidate);temp=Path(temp)
 try:
  def both(op,args={}):
   x=a.call(op,args);y=b.call(op,args);return y
  def frame(name):
   global trace_count
   a.call('test_render',dict(path=str(temp/'old'),full=True))
   got=b.call('test_resident_render',dict(path=str(temp/'new')))
   assert (temp/'old.scene').read_bytes()==(temp/'new.scene').read_bytes(),name
   ops=got['trace'][trace_count:];trace_count=len(got['trace'])
   # Nothing writes or copies after the final present; no eager mirror.
   presents=[i for i,t in enumerate(ops) if t['op']=='present']
   if presents:assert presents[-1]==len(ops)-1,(name,ops[presents[-1]:])
   if name=='unchanged':assert not ops,(name,ops)
   if name=='pixel_edit':assert ops[-1]['command']==2,ops[-1]
   if name=='launcher':
    assert not any(t['op']=='copy' for t in ops)
    batches=[t['count'] for t in ops if t['op']=='batch']
    assert batches==[44,44],batches
    assert sum(batches)==sum(t['op'] in ('fill','copy','tile_blend') for t in ops)
   if name=='unchanged':assert not any(t['op']=='batch' for t in ops)
   m=got['projection'];assert m['cpu_scene_words']==0 and m['cpu_result_download_words']==0 and m['cpu_output_shadow_words']==4480
   assert all(t['op']!='download' for t in got['trace']),name
   # Only per-tile fills, never a whole scene/scratch initialization.
   assert all(t['words']==256 for t in got['trace'] if t['op']=='fill')
   for t in got['trace']:
    if t['op']=='upload' and t['region'] in (3,4):assert t['offset']+t['words']<=4476
   results.append(dict(name=name,projection=m,operations=len(ops),batch_requests=[t['count'] for t in ops if t['op']=='batch'],present_commands=[t['command'] for t in ops if t['op']=='present'],copy_words=sum(t['words'] for t in ops if t['op']=='copy')));return got
  both('test_desktop',dict(op='tick',now=0));w=b.call('get_state')['windows'][0]['window_id']
  initial=frame('launcher');assert initial['projection']['packed_mask_tiles_total']==44
  assert initial['projection']['jobs_per_core']==[48,48]
  assert initial['projection']['device_ready_frames']==(0 if os.environ.get('BOAYO_READY_PLANS')=='0' else 1)
  assert initial['projection']['geometry_tiles_evaluated_total']==initial['projection']['mask_samples_evaluated_total']==0
  frame('unchanged');assert results[-1]['projection']['jobs_per_core']==[0,0]
  both('update_surface',dict(window_id=w,x=81,y=72,width=2,height=2,rgb24=base64.b64encode(bytes([211,12,73])*4).decode()));frame('pixel_edit')
  both('configure_window',dict(window_id=w,azimuth=12));frame('move')
  both('configure_window',dict(window_id=w,azimuth=0));frame('restore')
  shape=json.loads((plans/'manifest.json').read_text())['plans'][0]['shape']
  both('configure_window',dict(window_id=w,shape=[]));frame('reveal')
  both('configure_window',dict(window_id=w,shape=shape));frame('shape_restore')
  both('configure_window',dict(window_id=w,roll=15,width_deg=50));frame('roll_fov')
  both('configure_window',dict(window_id=w,roll=0,width_deg=42));frame('roll_restore')
  f=both('create_window',dict(title='overlap',surface_width=64,surface_height=64,width_deg=18,height_deg=14,decorated=False,always_on_top=True))['window_id']
  both('fill',dict(window_id=f,rgb=[0,64,255]));frame('overlap');assert results[-1]['projection']['solid_tiles_total']>0
  both('configure_window',dict(window_id=f,shape=[[0,0,64,64,20]]));frame('rounded_overlap')
  both('configure_window',dict(window_id=f,mapped=False));frame('hide')
  both('configure_window',dict(window_id=f,mapped=True));frame('show')
  both('destroy_window',dict(window_id=f));frame('remove')
  both('view_move',dict(yaw=2,pitch=2));frame('pointer')
  both('view_move',dict(yaw=12,pitch=-3));frame('pointer_move')
  both('destroy_window',dict(window_id=w));frame('remove_launcher')
  # Alternate buffers repeatedly with small source edits and move/remove transitions.
  f=both('create_window',dict(title='stress',surface_width=32,surface_height=32,width_deg=22,height_deg=18,decorated=False))['window_id']
  for i in range(12):
   both('fill',dict(window_id=f,rgb=[i*19%256,73,211]));frame('stress_fill_'+str(i))
   both('configure_window',dict(window_id=f,azimuth=(i%3)*5,mapped=i%4!=3));frame('stress_move_'+str(i))
  both('destroy_window',dict(window_id=f));frame('stress_remove')
 finally:a.close();b.close()
report=dict(result='PASS_RESIDENT_TILES',hardware=False,scenes=results,candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS_RESIDENT_TILES',len(results),'scenes, no CPU scene/readback/output pixel uploads, stale DDR initialized nonzero')
