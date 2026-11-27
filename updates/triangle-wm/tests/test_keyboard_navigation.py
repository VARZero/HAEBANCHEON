"""Real WM/app sockets and a PTY-backed boayo-input, no physical board."""
import json,mmap,os,pty,signal,subprocess,sys,tempfile,termios,time
from pathlib import Path
from example_test_support import Control,until
wm,apps,bridge=map(os.path.abspath,sys.argv[1:4])
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';catalog='/etc/boayo/apps.json' if os.environ.get('KEYBOARD_GUEST') else str(Path(__file__).resolve().parents[1]/'config/apps.json')
 env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_EXAMPLES_BIN':apps,'BOAYO_APPS':catalog}
 log=open(folder+'/log','w+');server=subprocess.Popen([wm,'--mock'],env=env,stdout=log,stderr=log)
 clients=[];processes=[];master=slave=None;pixels=None
 try:
  until(lambda:os.path.exists(path+'.control'));app=Control(path);ctl=Control(path+'.control');clients=[app,ctl]
  wid=app.do('create_window',dict(title='Focus test',surface_width=100,surface_height=100,width_deg=20,height_deg=14))['window_id']
  surface=app.call('create_surface',dict(window_id=wid,format='RGB332'));fd=os.open(surface['path'],os.O_RDWR);pixels=mmap.mmap(fd,surface['slot_bytes']*2);os.close(fd);pixels[:10000]=bytes([36])*10000
  app.do('commit_surface',dict(window_id=wid,slot=0,sequence=1,damage=[[0,0,100,100]]))
  targets=[dict(id=id,x=x,y=y,w=20,h=20) for id,x,y in [('a',5,5),('b',65,5),('c',5,65),('d',65,65)]]
  app.call('set_focusables',dict(window_id=wid,targets=targets));assert ctl.call('navigation_status')['target']=='a';app.call('poll_events')
  master,slave=pty.openpty();original=termios.tcgetattr(slave)
  listener=subprocess.Popen([bridge,'--tty',os.ttyname(slave)],env=env,stdout=log,stderr=log);processes.append(listener)
  until(lambda:termios.tcgetattr(slave)!=original)
  def serial(bytes,target):
   os.write(master,bytes);until(lambda:ctl.call('navigation_status')['target']==target)
  serial(b'\x1b[C','b');serial(b'\x1b[B','d');serial(b'\x1b[D','c');serial(b'\x1b[A','a')
  app.call('poll_events');os.write(master,b'\r\n')
  clicks=[]
  def clicked():
   clicks.extend(e for e in app.call('poll_events')['events'] if e['type']=='click');return clicks
  until(clicked);time.sleep(.1);clicked();assert len(clicks)==1 and clicks[0]['target']=='a',clicks
  request=dict(key='Enter',source='retry-test',sequence=7)
  assert ctl.call('input_nav',request)['accepted'];assert ctl.call('input_nav',request)['duplicate']
  assert len([e for e in app.call('poll_events')['events'] if e['type']=='click'])==1
  serial(b'\x1b[A','$close');serial(b'\x1b[B','a')
  targets[0]['enabled']=False;app.call('set_focusables',dict(window_id=wid,targets=targets));assert ctl.call('navigation_status')['target']=='b'
  listener.terminate();listener.wait(timeout=5);assert termios.tcgetattr(slave)==original,'TTY state was not restored'
  foreground=subprocess.Popen([bridge],stdin=slave,env=env,stdout=log,stderr=log);processes.append(foreground)
  until(lambda:termios.tcgetattr(slave)!=original);os.write(master,b'\x03');assert foreground.wait(timeout=5)==0
  assert termios.tcgetattr(slave)==original,'foreground stdin/Ctrl+C did not restore TTY'
  # Home opens the launcher. Dashboard remains an app launched from its icon.
  assert ctl.call('input_nav',dict(key='Home'))['accepted']
  def home():return [w for w in ctl.call('get_state')['windows'] if w['title']=='BoAYO launcher']
  def dashboard():return [w for w in ctl.call('get_state')['windows'] if w['title']=='BoAYO dashboard']
  opened=until(home)[0]['window_id'];until(lambda:ctl.call('navigation_status')['window_id']==opened)
  until(lambda:home()[0]['phase']=='content');assert not dashboard()
  assert ctl.call('navigation_status')['target']=='reload'
  ctl.call('input_nav',dict(key='ArrowDown'));assert ctl.call('navigation_status')['target']=='app:dashboard'
  ctl.call('input_nav',dict(key='Enter'));dash=until(dashboard)[0]['window_id']
  until(lambda:ctl.call('get_state')['focused_window']==dash)
  assert ctl.call('input_nav',dict(key='Home'))['window_id']==opened
  for _ in range(3):ctl.call('input_nav',dict(key='Home'))
  assert len(home())==1 and len(dashboard())==1 and ctl.call('get_state')['focused_window']==opened
  assert ctl.call('navigation_status')['target']=='reload'
  assert home()[0]['controls_enabled']==False
  assert not [c for c in ctl.call('input_status')['controls'] if c['window_id']==opened]
  ctl.call('input_nav',dict(key='ArrowUp'));assert ctl.call('navigation_status')['target']=='reload'
  ctl.call('input_nav',dict(key='Enter'));assert len(home())==1
  # Ordinary app windows still expose all three controls and keyboard close.
  ctl.do('raise_window',dict(window_id=dash));until(lambda:dashboard()[0]['phase']=='content')
  assert dashboard()[0]['controls_enabled']==True
  assert len([c for c in ctl.call('input_status')['controls'] if c['window_id']==dash])==3
  ctl.call('input_nav',dict(key='ArrowUp'));assert ctl.call('navigation_status')['target']=='$close'
  assert ctl.call('input_nav',dict(key='Enter'))['closed']==dash;until(lambda:not dashboard())
  assert len(home())==1
  print('PASS_KEYBOARD_NAVIGATION real-TTY arrows Enter CRLF dedup disabled-target close Home-launcher captionless-home dashboard-icon TTY-restoration')
 finally:
  for child in processes:
   if child.poll() is None:child.terminate();child.wait(timeout=5)
  if pixels is not None:pixels.close()
  for client in clients:client.close()
  if server.poll() is None:server.terminate();server.wait(timeout=5)
  if master is not None:os.close(master)
  if slave is not None:os.close(slave)
  log.close()
