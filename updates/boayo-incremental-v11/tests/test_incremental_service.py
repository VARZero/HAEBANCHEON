"""Public IPC state stays complete while the worker publishes incremental updates."""
import json,os,socket,subprocess,sys,tempfile,time
from pathlib import Path
binary=sys.argv[1]
with tempfile.TemporaryDirectory() as td,open('/dev/null','w') as log:
 path=td+'/wm.sock';env=dict(os.environ,BOAYO_SOCKET=path)
 p=subprocess.Popen([binary,'serve','--mock'],env=env,stdout=log,stderr=log)
 try:
  end=time.monotonic()+60
  while not Path(path).exists():assert p.poll() is None and time.monotonic()<end;time.sleep(.02)
  def connect():
   s=socket.socket(socket.AF_UNIX);s.settimeout(30);s.connect(path);return s,s.makefile('rwb',buffering=0)
  s,f=connect();seq=0
  def call(op,args={},owner='incremental-test'):
   global seq
   seq+=1;f.write((json.dumps(dict(id=seq,app=owner,op=op,args=args))+'\n').encode());r=json.loads(f.readline());assert r['ok'],r;return r['result']
  while not call('get_state').get('performance',{}).get('render_count'):
   assert time.monotonic()<end;time.sleep(.05)
  ids=[call('create_window',dict(title='incremental '+str(i),surface_width=32,surface_height=32,decorated=False))['window_id'] for i in range(2)]
  def window(wid):return next(w for w in call('get_state')['windows'] if w['window_id']==wid)
  for wid in ids:
   call('configure_window',dict(window_id=wid,title='changed',azimuth=20,elevation=-8,roll=12))
   w=window(wid);assert (w['title'],w['azimuth'],w['elevation'],w['roll'])==('changed',20,-8,12)
   call('fill',dict(window_id=wid,rgb=[12,34,56]));assert window(wid)['title']=='changed'
   call('configure_window',dict(window_id=wid,mapped=False));assert not window(wid)['mapped']
   call('configure_window',dict(window_id=wid,mapped=True));assert window(wid)['mapped']
  call('input_button',dict(button='left',pressed=True))
  assert call('capture_pointer',dict(window_id=ids[0]));assert call('get_state')['pointer']['captured_window']==ids[0]
  call('release_pointer');assert call('get_state')['pointer']['captured_window'] is None
  call('input_button',dict(button='left',pressed=False))
  call('focus_window',dict(window_id=ids[0]));assert window(ids[0])['focused']
  call('destroy_window',dict(window_id=ids[1]));assert all(w['window_id']!=ids[1] for w in call('get_state')['windows'])
  f.close();s.close();s,f=connect();end=time.monotonic()+5
  while any(w['window_id']==ids[0] for w in call('get_state',owner='observer')['windows']):
   assert time.monotonic()<end;time.sleep(.05)
  state=call('get_state',owner='observer');assert state['app_error'] is None and state['render_error'] is None
  call('shutdown',owner='observer');assert p.wait(timeout=30)==0
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=30)
print('PASS_INCREMENTAL_SERVICE: create, metadata, pixels, map, focus, capture, deletion, disconnect; complete public snapshots')
