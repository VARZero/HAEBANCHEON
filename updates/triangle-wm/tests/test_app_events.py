"""Real socket input ownership, bounded FIFO, long waits and WM isolation."""
import json,os,select,socket,subprocess,sys,tempfile,mmap,time
import binary_test_wire as wire
class Peer:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(10);self.s.connect(path);self.f=self.s.makefile('rb');self.seq=0
 def send(self,op,args=None):
  self.seq+=1;self.s.sendall(wire.encode(dict(id=self.seq,op=op,args=args or {})))
 def read(self,ok=True):
  r=wire.read(self.f);assert r['ok']==ok,r;return r.get('result',r)
 def call(self,op,args=None,ok=True):self.send(op,args);return self.read(ok)
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
  return r
 def close(self):self.f.close();self.s.close()
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';p=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path},stdout=subprocess.PIPE,text=True);assert p.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
 a,b,ctl=Peer(path),Peer(path),Peer(path+'.control');buffers=[]
 try:
  ids=[]
  for app in [a,b]:
   wid=app.do('create_window',dict(surface_width=64,surface_height=48,width_deg=20,height_deg=14))['window_id'];ids.append(wid)
   info=app.call('create_surface',dict(window_id=wid,format='RGB332'));fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd);buffers.append(pixels)
   pixels[:3072]=bytes([84])*3072;app.do('commit_surface',dict(window_id=wid,slot=0,sequence=1,damage=[[0,0,64,48]]))
  before=ctl.call('get_state')['performance']['publications'];buffers[1][:3072]=bytes([36])*3072
  b.do('commit_surface',dict(window_id=ids[1],slot=0,sequence=2,damage=[[0,0,64,48]],background_index=36,splash_index=3))
  state=ctl.call('get_state');assert state['performance']['publications']==before+1;assert state['windows'][1]['background_index']==36 and state['windows'][1]['splash_index']==3
  b.call('commit_surface',dict(window_id=ids[1],slot=0,sequence=3,damage=[[0,0,1,1]],background_index=300),False)
  assert b.call('create_surface',dict(window_id=ids[1]))['sequence']==2,'invalid frame style consumed the slot sequence'
  status=ctl.call('input_status');assert all(c['hit_size']==48 and c['icon_size']==28 for c in status['controls'])
  a.call('input_text',dict(text='wrong owner'),False);ctl.call('input_text',dict(text='한글'),False)
  ctl.call('poll_events',ok=False)
  b.send('wait_events',dict(timeout_ms=1000));ctl.do('configure_window',dict(window_id=ids[1],splash_index=38))
  assert not select.select([b.s],[],[],.03)[0],'scene publication incorrectly completed an event wait'
  ctl.call('input_text',dict(text='typed'));events=b.read()['events'];assert events==[dict(type='text',text='typed',window_id=ids[1])];assert not a.call('poll_events')['events']
  ctl.do('input_visibility',dict(visible=False));ctl.do('input_button',dict(x=640,y=360,pressed=True));ctl.do('input_button',dict(x=640,y=360,pressed=False))
  e=b.call('wait_events',dict(timeout_ms=100))['events'];assert len(e)==1 and e[0]['type']=='click' and e[0]['window_id']==ids[1];assert abs(e[0]['x']-32)<=1 and abs(e[0]['y']-24)<=1,e
  for i in range(128):ctl.call('input_key',dict(key='ArrowLeft'))
  ctl.call('input_text',dict(text='overflow'),False);events=b.call('poll_events')['events'];assert len(events)==128 and all(e['key']=='ArrowLeft' for e in events)
  # Newer key presses are ordered, never merged like pointer motion.
  for key in ['ArrowRight','Backspace','Save']:ctl.call('input_key',dict(key=key))
  assert [e['key'] for e in b.call('poll_events')['events']]==['ArrowRight','Backspace','Save']
  h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='move');ctl.do('input_button',dict(x=h['x'],y=h['y'],pressed=True));ctl.call('input_text',dict(text='during drag'),False);ctl.do('input_cancel')
  assert not b.call('poll_events')['events'],'WM drag leaked into application input'
  b.send('wait_events',dict(timeout_ms=1000));ctl.do('destroy_window',dict(window_id=ids[1]));assert b.read()['events']==[dict(type='closed',window_id=ids[1])]
  start=time.monotonic();assert not a.call('wait_events',dict(timeout_ms=40))['events'];assert time.monotonic()-start<2
  print('PASS_APP_EVENTS: owner-only queues, empty long wait, FIFO capacity/error, source click mapping, WM priority, close and timeout')
 finally:
  for buf in buffers:buf.close()
  a.close();b.close();ctl.call('shutdown');ctl.close();assert p.wait(timeout=10)==0
