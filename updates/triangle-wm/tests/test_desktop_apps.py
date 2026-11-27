"""Real new apps and child processes against a local mock WM. No board access."""
import json,os,socket,subprocess,sys,tempfile,time
from example_test_support import Control,until,screen

with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary','BOAYO_APPS':sys.argv[3]}
 wm=subprocess.Popen([sys.argv[1],'--mock'],env=env,stdout=subprocess.PIPE,text=True);assert wm.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
 ctl=Control(path+'.control');observer=Control(path);app=None
 try:
  for kind in ['launcher','control','terminal']:
   app=subprocess.Popen([sys.argv[2],kind],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   ready=app.stdout.readline();assert ready.startswith('EXAMPLE_READY '),(ready,app.stderr.read() if app.poll() is not None else '')
   wid=json.loads(ready[len('EXAMPLE_READY '):])['window_id'];assert ctl.call('get_state')['windows'][0]['phase']=='content'
   def click(x,y,yaw=0,pitch=0):
    xy=screen(x,y,yaw,pitch);ctl.do('input_button',dict(**xy,pressed=True));ctl.do('input_button',dict(**xy,pressed=False))
   if kind=='launcher':
    click(60,90);until(lambda:len(ctl.call('get_state')['windows'])==2)
    child=next(w['window_id'] for w in ctl.call('get_state')['windows'] if w['window_id']!=wid)
    until(lambda:all(w['phase']=='content' for w in ctl.call('get_state')['windows']))
    ctl.do('destroy_window',dict(window_id=child));until(lambda:len(ctl.call('get_state')['windows'])==1)
   elif kind=='control':
    ctl.call('view_set',dict(yaw=8,pitch=4));assert observer.call('app_events')['view'][:2]==[8,4]
    click(140,90,8,4)
    until(lambda:observer.call('app_events')['view'][:2]==[0,0])
    click(150,170);click(150,170);time.sleep(.1);assert app.poll() is None and ctl.call('get_state')['service']['mock']
   else:
    marker=folder+'/shell-result';ctl.call('input_text',dict(text="printf 'real-%s' terminal > "+marker));ctl.call('input_key',dict(key='Enter'))
    until(lambda:os.path.exists(marker) and open(marker).read()=='real-terminal')
    ctl.call('input_key',dict(key='Interrupt'));ctl.call('input_key',dict(key='PageUp'));ctl.call('input_key',dict(key='PageDown'))
   ctl.do('destroy_window',dict(window_id=wid));assert app.wait(timeout=10)==0,app.stderr.read();app=None
   until(lambda:not ctl.call('get_state')['windows'])
  print('PASS_DESKTOP_APPS: real launcher child window, control actions blocked from poweroff in mock, real terminal shell command and close')
 finally:
  if app and app.poll() is None:app.terminate();app.wait(timeout=10)
  ctl.call('shutdown');ctl.close();observer.close();assert wm.wait(timeout=10)==0
