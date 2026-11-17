import sys,json,subprocess,tempfile,base64
from pathlib import Path
import numpy as np
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('platform','wm','apps')]
from bosio_window_manager import SphericalWindowManager
from bosio_hw_compositor import HardwareCompositor
from projection_model import reference_job
from kfb_projectors import HostBuffer
class ReferenceScheduler:
 def run(self,texture,descriptors,scene,**kw):
  jobs=[0,0];cells=[0,0]
  for i,d in enumerate(descriptors):jobs[i%2]+=1;cells[i%2]+=reference_job(texture,scene,d)
  return dict(status=[0,0],jobs_per_core=jobs,written_cells_per_core=cells,milliseconds=0,total_call_ms=0)
 def close(self):pass
wm=SphericalWindowManager(32)
hw=HardwareCompositor(None,wm._rays,scheduler=ReferenceScheduler(),allocator=HostBuffer)
proc=subprocess.Popen([sys.argv[1],'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
def request(op,args=None):
 proc.stdin.write(json.dumps(dict(op=op,args=args or {},app='test'))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['ok'],r;return r['result']
checks=[]
with tempfile.TemporaryDirectory() as tmp:
 def render(label):
  order=[i for i in wm.z_order if wm.windows[i].mapped]
  expected,n,kind=hw.render_update(wm.windows,order,wm.focused_window,(wm.pointer_azimuth,wm.pointer_elevation,wm.pointer_visible),wm.background,32)
  got=request('test_render',dict(path=str(Path(tmp)/'frame')))
  np.testing.assert_array_equal(np.fromfile(Path(tmp)/'frame.scene',dtype='<u4'),hw.scene)
  np.testing.assert_array_equal(np.fromfile(Path(tmp)/'frame.update',dtype='<u4'),expected)
  # Clipped tiles now skip projection. Keep the Python renderer unchanged as
  # the pixel oracle; scheduling/work counters intentionally differ.
  for key in ('changed_tiles','active_tiles','update_kind'):
   assert got['projection'][key]==hw.metrics[key],(label,key,got['projection'][key],hw.metrics[key])
  assert sum(got['projection']['jobs_per_core'])<=sum(hw.metrics['jobs_per_core'])
  assert sum(got['projection']['written_cells_per_core'])<=sum(hw.metrics['written_cells_per_core'])
  hw.ack_presented();checks.append(dict(case=label,jobs=got['projection']['jobs_per_core'],kind=kind,words=len(expected)))
 render('empty')
 for i,(az,el) in enumerate([(0,0),(10,3),(-170,28)]):
  a=dict(title=f'layer{i}',azimuth=az,elevation=el,width_deg=42,height_deg=30,surface_width=64,surface_height=48)
  wm.create_window('test',**a);request('create_window',a)
  rgb=np.random.default_rng(i).integers(0,256,(48,64,3),dtype=np.uint8)
  a=dict(window_id=i+1,x=0,y=0,width=64,height=48,rgb24=base64.b64encode(rgb.tobytes()).decode());wm.update_surface('test',**a);request('update_surface',a)
 render('three_windows_two_engines')
 render('unchanged_no_jobs')
 a=dict(window_id=1,x=15,y=10,width=3,height=2,rgb24=base64.b64encode(bytes([255,40,80])*6).decode());wm.update_surface('test',**a);request('update_surface',a);render('dirty_subrectangle')
 a=dict(window_id=2,shape=[[0,0,64,48,12]]);wm.configure_window('test',**a);request('configure_window',a);render('shape_only_no_reprojection')
 wm.raise_window('test',1);request('raise_window',dict(window_id=1));render('stacking_order')
 a=dict(window_id=1,azimuth=35,elevation=-15);wm.configure_window('test',**a);request('configure_window',a);render('geometry_move')
 a=dict(window_id=2,mapped=False);wm.configure_window('test',**a);request('configure_window',a);render('hide_layer')
 wm.destroy_window('test',1);request('destroy_window',dict(window_id=1));render('destroy_layer')
 request('view_move',dict(yaw=2,pitch=2));wm.pointer_warp(2,2);render('pose_only_no_reprojection')
proc.stdin.close();assert proc.wait()==0
print(json.dumps(dict(result='PASS_MOCK_BACKEND',frames=checks,pixels='bit-exact full fixed-slot scenes and BPT2 patches',hardware=False)))
