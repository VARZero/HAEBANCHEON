import os,sys,json,socket,time,subprocess,tempfile
from pathlib import Path
binary,path=sys.argv[1:3]
with tempfile.TemporaryDirectory() as td:
 sockpath=td+'/wm.sock';env=dict(os.environ,BOAYO_SOCKET=sockpath)
 p=subprocess.Popen([binary,'serve','--mock'],env=env,stdout=open(td+'/log','w'),stderr=subprocess.STDOUT)
 try:
  end=time.monotonic()+60
  while not Path(sockpath).exists():
   assert p.poll() is None and time.monotonic()<end;time.sleep(.05)
  def connect():
   s=socket.socket(socket.AF_UNIX);s.settimeout(30);s.connect(sockpath);return s,s.makefile('rwb',buffering=0)
  s,f=connect();seq=0
  def call(op,args={},ok=True,stream=None):
   global seq
   seq+=1;q=stream or f;q.write((json.dumps(dict(id=seq,app='test',op=op,args=args))+'\n').encode());r=json.loads(q.readline());assert r['ok']==ok,r;return r.get('result')
  while True:
   state=call('get_state')
   if state.get('performance',{}).get('rendered_generation',0)==state.get('generation',-1) and state.get('output',{}).get('commits',0)>=2:break
   assert time.monotonic()<end;time.sleep(.1)
  call('view_play',dict(path=path,duration=2))
  call('launch_app',dict(kind='dashboard'),False)
  call('view_play',dict(path=path,duration=2),False)
  time.sleep(.5);r=call('view_progress');assert r['active'] and r['submitted']>2,r
  end=time.monotonic()+8
  while call('view_progress')['active']:assert time.monotonic()<end;time.sleep(.1)
  assert call('view_state')==dict(yaw=0.,pitch=0.)
  bad=Path(td)/'bad';bad.write_bytes(Path(path).read_bytes()[:-1]);call('view_play',dict(path=str(bad)),False)
  bad.write_bytes(Path(path).read_bytes()[:-1]+bytes([Path(path).read_bytes()[-1]^1]));call('view_play',dict(path=str(bad)),False)
  call('view_play',dict(path=path,duration=10));call('view_stop');assert not call('view_progress')['active']
  call('view_play',dict(path=path,duration=10));f.close();s.close();time.sleep(.2)
  s,f=connect();end=time.monotonic()+5
  while call('view_progress')['active']:assert time.monotonic()<end;time.sleep(.1)
  call('shutdown');assert p.wait(timeout=30)==0
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=30)
print('PASS service preview, query, busy guard, natural stop, malformed/checksum, explicit stop, disconnect restore')
