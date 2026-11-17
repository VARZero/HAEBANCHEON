import os,sys,json,subprocess,tempfile,time,socket,pty,select,termios
from pathlib import Path
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('wm','apps','platform')]
from bosio_wm_client import BosioWMClient
binary=str(Path(sys.argv[1]).resolve());steps=[]
with tempfile.TemporaryDirectory(prefix='boayo-') as tmp:
 path=str(Path(tmp)/'wm.sock');env=dict(os.environ,BOAYO_SOCKET=path)
 with open(Path(tmp)/'server.log','w+') as log:
  proc=subprocess.Popen([binary,'serve','--mock'],env=env,stdout=log,stderr=log)
  try:
   end=time.monotonic()+30
   while not Path(path).exists():
    if proc.poll() is not None:log.seek(0);raise AssertionError(log.read())
    assert time.monotonic()<end,'server startup timeout';time.sleep(.05)
   with BosioWMClient('compatibility',socket_path=path,timeout=30) as c:
    assert c.ping()['runtime']=='C++17'
    def wait(predicate):
     end=time.monotonic()+30
     while time.monotonic()<end:
      state=c.get_state();assert state['render_error'] is None and state.get('app_error') is None,state
      if predicate(state):return state
      time.sleep(.05)
     raise AssertionError('state timeout: '+json.dumps(state))
    initial=wait(lambda s:s['output']['commits']>=2 and s['projection'].get('total_hw_jobs',0)>0)
    assert initial['output']['mock_backend'];steps.append('launcher via both modeled projection engines and output')
    for kind,title in [('dashboard','Dashboard'),('telemetry','Telemetry'),('pulse','SDK Pulse')]:
     wid=c.call('launch_app',kind=kind);state=wait(lambda s:any(w['window_id']==wid and w['title']==title for w in s['windows']));steps.append('launch '+kind)
    app=next(w for w in state['windows'] if w['title']=='SDK Pulse')
    c.request_window_action(app['window_id'],'minimize')
    state=wait(lambda s:any(w['workspace'].get('role')=='storage' for w in s['windows']))
    c.request_window_action(app['window_id'],'restore',azimuth=15,elevation=10)
    state=wait(lambda s:any(w['window_id']==app['window_id'] and w['mapped'] and w['azimuth']==15 for w in s['windows']));steps.append('minimize storage restore')
    c.request_window_action(app['window_id'],'close');wait(lambda s:all(w['window_id']!=app['window_id'] for w in s['windows']));steps.append('close app and captions')
    c.call('show_launcher');c.pointer_warp(160,0);c.call('input_click',button='left')
    wait(lambda s:all(not w['mapped'] for w in s['windows'] if w['title']=='BoAYO Launcher'))
    c.call('show_launcher');c.call('view_reset');before_click=c.get_state()['pointer']['left_release_serial']
    master,slave=pty.openpty();saved=termios.tcgetattr(slave)
    keyproc=subprocess.Popen([binary,'keys'],env=env,stdin=slave,stdout=slave,stderr=slave)
    text=bytearray()
    def until(token,timeout=30):
     end=time.monotonic()+timeout
     while token not in text:
      assert time.monotonic()<end,(token,text[-2000:]);assert keyproc.poll() is None,(keyproc.returncode,text)
      if select.select([master],[],[],.1)[0]:text.extend(os.read(master,65536))
    until(b'BOAYO_KEYS_READY')
    for key in b'dwas':
     text.clear();os.write(master,bytes([key]));until(b'"yaw"')
    text.clear();os.write(master,b'\r\n');until(b'"left_release_serial"');time.sleep(.1)
    assert c.call('view_state')=={'yaw':0.,'pitch':0.}
    assert c.get_state()['pointer']['left_release_serial']==before_click+1,'CRLF double click'
    os.write(master,b'rq');assert keyproc.wait(timeout=30)==0;assert termios.tcgetattr(slave)==saved,'TTY not restored';os.close(master);os.close(slave);steps.append('PTY WASD CRLF one click reset quit terminal restore')
    c.call('shutdown')
   assert proc.wait(timeout=30)==0
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=30)
print(json.dumps(dict(result='PASS_MOCK_BACKEND',checks=steps,hardware=False)))
