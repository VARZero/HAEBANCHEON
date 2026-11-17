"""Preserve the existing scalar C++ fallback for decorations and oversized surfaces."""
import sys,os,json,subprocess,tempfile,base64
from pathlib import Path
import numpy as np
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('wm','apps','platform')]
with tempfile.TemporaryDirectory() as tmp:
 lib=Path(tmp)/'reference.so'
 subprocess.run(['g++','-O2','-ffp-contract=off','-shared','-fPIC',str(here/'reference/wm/native/bosio_compositor.cpp'),'-o',str(lib)],check=True)
 os.environ['BOSIO_COMPOSITOR_LIB']=str(lib)
 from bosio_window_manager import SphericalWindowManager
 wm=SphericalWindowManager(32)
 assert wm.native is not None
 proc=subprocess.Popen([sys.argv[1],'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 def req(op,args):
  proc.stdin.write(json.dumps(dict(op=op,args=args,app='test'))+'\n');proc.stdin.flush();r=json.loads(proc.stdout.readline());assert r['ok'],r;return r['result']
 a=dict(title='decorated',surface_width=32,surface_height=24,decorated=True,azimuth=13,elevation=-7)
 wm.create_window('test',**a);req('create_window',a)
 a=dict(window_id=1,rgb=[160,192,32]);wm.fill('test',**a);req('fill',a)
 for step in range(3):
  if step:
   a=dict(window_id=1,azimuth=step*15,roll=step*7);wm.configure_window('test',**a);req('configure_window',a)
  expected,_=wm.render_packed();r=req('test_render',dict(path=str(Path(tmp)/'frame')))
  actual=np.fromfile(Path(tmp)/'frame.update',dtype='<u4');np.testing.assert_array_equal(actual,expected)
  assert r['projection']['fallback_frames']==step+1
 proc.stdin.close();assert proc.wait()==0
 wm.native.close()
print(json.dumps(dict(result='PASS',frames=3,scope='unchanged native fallback; bit-exact Python-driven comparison')))
