"""Real socket controls, ownership, clean overlay restoration and capture lifecycle."""
import json,os,socket,subprocess,sys,tempfile,mmap,hashlib
trace=[]

class Client:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path);self.f=self.s.makefile('rwb',buffering=0);self.seq=0
 def call(self,op,args=None,ok=True):
  self.seq+=1;self.f.write((json.dumps(dict(id=self.seq,op=op,args=args or {}))+'\n').encode());r=json.loads(self.f.readline());assert r['ok']==ok,r;return r.get('result',r)
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',{'generation':r['generation']})
  if os.environ.get('INPUT_TRACE_PATH') and 'ctl' in globals():trace.append(hashlib.sha256(json.dumps(ctl.call('snapshot')['tile_hashes']).encode()).hexdigest())
  return r
 def close(self):self.f.close();self.s.close()

with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';proc=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_INPUT':'1'},stdout=subprocess.PIPE,text=True)
 try:
  assert proc.stdout.readline().startswith('TRIANGLE_BOAYO_READY');app=Client(path);ctl=Client(path+'.control')
  first=app.do('create_window',{'width_deg':20,'height_deg':14,'surface_width':32,'surface_height':32})['window_id']
  info=app.call('create_surface',{'window_id':first,'format':'RGB332'});fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd)
  pixels[:1024]=bytes(range(256))*4;app.do('commit_surface',{'window_id':first,'slot':0,'sequence':1,'damage':[[0,0,32,32]]})
  app.call('input_move',{'x':100,'y':100},ok=False)
  def status():return ctl.call('input_status')
  def button(action):return next(c for c in status()['controls'] if c['window_id']==first and c['action']==action)
  def move(x,y):return ctl.do('input_move',{'x':x,'y':y})
  def press(c):return ctl.do('input_button',{'x':c['x'],'y':c['y'],'pressed':True})
  def release(x,y):return ctl.do('input_button',{'x':x,'y':y,'pressed':False})
  before=status();ctl.call('input_move',{'x':1280,'y':360},ok=False);assert status()==before,'invalid coordinates changed pointer'
  c=button('move');press(c);rival=Client(path+'.control');rival.call('input_move',{'x':20,'y':20},ok=False);rival.close();assert status()['captured_window']==first;ctl.do('input_cancel')
  ctl.do('input_cancel',{'hide':True});base=ctl.call('snapshot')['tile_hashes']
  for x,y in [(640,360),(10,10),(1270,710),(600,400)]:move(x,y)
  # Sweep tile edges and revisit the same samples to exercise warm caches.
  route=[(0,0),(1279,719),(0,719),(1279,0)]+[(x,360+(i%3-1)*31) for i,x in enumerate(range(460,821,30))]
  for x,y in route+route[::-1]:move(x,y)
  ctl.do('input_cancel',{'hide':True});assert base==ctl.call('snapshot')['tile_hashes'],'cursor trail'
  move(640,360);pixels[:1024]=bytes(reversed(range(256)))*4;app.do('commit_surface',{'window_id':first,'slot':0,'sequence':2,'damage':[[0,0,32,32]]});ctl.do('input_cancel',{'hide':True});updated=ctl.call('snapshot')['tile_hashes'];assert updated!=base
  move(630,360);move(680,390);ctl.do('input_cancel',{'hide':True});assert updated==ctl.call('snapshot')['tile_hashes'],'stale under-cursor content'
  # A pending scene mutation followed by input in one read must retain damage.
  requests=[('configure_window',{'window_id':first,'azimuth':2}),('input_move',{'x':611,'y':373})]
  wire=[]
  for op,args in requests:
   ctl.seq+=1;wire.append(json.dumps(dict(id=ctl.seq,op=op,args=args))+'\n')
  ctl.f.write(''.join(wire).encode())
  for _ in requests:
   result=json.loads(ctl.f.readline());assert result['ok'],result
  ctl.call('wait_present',{'generation':result['result']['generation']})
  ctl.do('input_cancel',{'hide':True})
  moved=ctl.call('snapshot')['tile_hashes'];assert moved!=updated,'queued geometry was lost'
  ctl.do('configure_window',{'window_id':first,'azimuth':0});assert updated==ctl.call('snapshot')['tile_hashes']
  # A first partial RGB332 commit must retain background outside the patch.
  partial=app.do('create_window',{'surface_width':32,'surface_height':32,'azimuth':-7,'background_index':164})['window_id']
  info2=app.call('create_surface',{'window_id':partial,'format':'RGB332'});fd2=os.open(info2['path'],os.O_RDWR);partial_pixels=mmap.mmap(fd2,info2['slot_bytes']*2);os.close(fd2)
  for y in range(4,8):partial_pixels[y*32+4:y*32+8]=b'\xff'*4
  r=app.do('commit_surface',{'window_id':partial,'slot':0,'sequence':1,'damage':[[4,4,4,4]]});assert r['changed']
  r=app.do('commit_surface',{'window_id':partial,'slot':0,'sequence':2,'damage':[[4,4,4,4]]});assert not r['changed']
  app.do('destroy_window',{'window_id':partial});partial_pixels.close();assert updated==ctl.call('snapshot')['tile_hashes']
  second=app.do('create_window',{'width_deg':20,'height_deg':14,'azimuth':7,'background_index':164})['window_id'];app.do('destroy_window',{'window_id':second});assert updated==ctl.call('snapshot')['tile_hashes'],'overlap restore'
  # Exercise cached normal/hover/pressed button masks with and without a dot.
  for shown in [False,True]:
   ctl.do('input_visibility',{'visible':shown})
   for action in ['close','move','resize']:
    c=button(action)
    for dx in [40,0,15,0,-40]:move(c['x']+dx,c['y'])
    press(c);ctl.do('input_cancel');move(10,10)
  ctl.do('input_cancel',{'hide':True});assert updated==ctl.call('snapshot')['tile_hashes'],'button cache left stale pixels'
  close=button('close');press(close);release(close['x']+100,close['y']);assert len(ctl.call('get_state')['windows'])==1
  c=button('move');press(c);move(c['x']+40,c['y']-20)
  assert ctl.call('get_state')['windows'][0]['azimuth']==0,'committed before release'
  assert release(c['x']+40,c['y']-20)['applied'];w=ctl.call('get_state')['windows'][0];assert w['azimuth']!=0
  c=button('resize');press(c);assert release(c['x']+40,c['y']+25)['applied'];w=ctl.call('get_state')['windows'][0];assert w['width_deg']>20 and w['height_deg']>14
  for width in [8,12,20,28]:
   for yaw in [-5,0,5]:
    for pitch in [-3,0,3]:
     ctl.do('configure_window',{'window_id':first,'width_deg':width,'height_deg':width*.7,'azimuth':yaw,'elevation':pitch})
     assert len(status()['controls'])==3,('missing handle',width,yaw,pitch,status())
  ctl.do('configure_window',{'window_id':first,**{k:w[k] for k in ['width_deg','height_deg','azimuth','elevation']}})
  c=button('move');press(c);ctl.close();ctl=Client(path+'.control');assert not status()['pressed']
  # Pose updates rebuild the screen-space overlay without modifying source pixels.
  for y in [-5,5,0]:ctl.do('view_set',{'yaw':y,'pitch':2 if y else 0})
  assert len(status()['controls'])==3
  if os.environ.get('TEST_INPUT_VISIBILITY'):
   def extra(op,args=None):
    r=ctl.call(op,args)
    if 'generation' in r:ctl.call('wait_present',{'generation':r['generation']})
    return r
   original=status();original_hash=ctl.call('snapshot')['tile_hashes']
   app.call('input_visibility',{'visible':False},ok=False)
   ctl.call('input_visibility',{'visible':1},ok=False);assert status()==original
   extra('input_visibility',{'visible':False});hidden=ctl.call('snapshot')['tile_hashes'];assert hidden!=original_hash
   extra('input_move',{'x':5,'y':5});hidden=ctl.call('snapshot')['tile_hashes']
   generation=status()['generation']
   for x,y in [(5,5),(40,30),(10,10)]:
    extra('input_move',{'x':x,'y':y});state=status()
    assert not state['visible'] and not state['display_enabled'] and (state['x'],state['y'])==(x,y)
    assert state['generation']==generation,'hidden motion unnecessarily published'
    assert ctl.call('snapshot')['tile_hashes']==hidden
   c=button('move');extra('input_button',{'x':c['x'],'y':c['y'],'pressed':True})
   assert status()['captured_window']==first and status()['pressed']
   extra('input_visibility',{'visible':True});assert status()['pressed'] and status()['captured_window']==first
   extra('input_visibility',{'visible':False});extra('input_move',{'x':c['x']+20,'y':c['y']})
   assert status()['pressed'] and not status()['visible']
   extra('input_cancel');extra('input_move',{'x':original['x'],'y':original['y']})
   extra('input_visibility',{'visible':True});assert ctl.call('snapshot')['tile_hashes']==original_hash
   print('PASS_INPUT_VISIBILITY: hidden tracking, no redundant publication, capture retained, restored pixels')
  c=button('close');press(c);assert release(c['x'],c['y'])['applied'];assert not ctl.call('get_state')['windows']
  ctl.do('input_cancel',{'hide':True});hashes=ctl.call('snapshot')['tile_hashes'];assert len(set(hashes))==1,'close/cursor left visible tiles'
  pixels.close();app.close();ctl.call('shutdown');ctl.close();assert proc.wait(timeout=10)==0
  print('PASS_INPUT: placement, permission, no trails, close cancellation, commit-on-release, move, resize, disconnect, pose, close')
  if os.environ.get('INPUT_TRACE_PATH'):
   with open(os.environ['INPUT_TRACE_PATH'],'w') as f:json.dump(trace,f)
 finally:
  if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
