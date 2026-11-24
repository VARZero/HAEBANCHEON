"""Raw one-way packets: fragmentation, newest-only, ordering and capture safety."""
import os,signal,socket,struct,subprocess,sys,tempfile
import binary_test_wire as wire

class Client:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path)
  self.f=self.s.makefile('rb');self.seq=0
 def encode(self,op,args=None):
  self.seq+=1;return wire.encode(dict(id=self.seq,op=op,args=args or {}))
 def call(self,op,args=None):
  self.s.sendall(self.encode(op,args));r=wire.read(self.f)
  assert r['ok'] and r['id']==self.seq,r # PTR1 must never leave unsolicited replies.
  return r['result']
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
  return r
 def close(self):self.f.close();self.s.close()
def packet(seq,x,y):return struct.pack('<III IHH',0x314d5742,12,0x31525450,seq,x,y)

with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';p=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary'},stdout=subprocess.PIPE,text=True)
 try:
  assert p.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
  app=Client(path);ctl=Client(path+'.control');wid=app.do('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32))['window_id']
  assert ctl.call('input_status')['fast_motion']
  ctl.do('input_visibility',dict(visible=False))
  h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='move');x,y=h['x'],h['y']
  ctl.do('input_button',dict(x=x,y=y,pressed=True));base=ctl.call('get_state')['performance']['last_render']
  os.kill(p.pid,signal.SIGSTOP)
  try:
   raw=b''.join(packet(i,x+i*3,y) for i in range(1,17))
   ctl.s.sendall(raw[:7]);ctl.s.sendall(raw[7:33]);ctl.s.sendall(raw[33:])
  finally:os.kill(p.pid,signal.SIGCONT)
  progress=ctl.call('input_progress');assert progress['fast_accepted_sequence']==16,progress
  done=ctl.call('wait_input',dict(ticket=progress['accepted_ticket']))
  assert done['fast_visible_sequence']==16 and done['coalesced_moves']>=15,done
  state=ctl.call('input_status');assert (state['x'],state['y'])==(x+48,y) and state['preview']['colored_cells_per_tile']==1024
  after=ctl.call('get_state')['performance']['last_render']
  for field in ['plan_builds_total','run_submissions_total','footprint_calls_total']:assert base[field]==after[field],field
  ctl.s.sendall(packet(15,1,1)+packet(16,2,2));assert ctl.call('input_progress')['accepted_ticket']==progress['accepted_ticket']
  rival=Client(path+'.control');rival.s.sendall(packet(1,30,30));assert rival.s.recv(1)==b'';rival.close()
  assert ctl.call('input_status')['pressed'],'rival disconnected legitimate capture'
  # A button is an ordered barrier, including pending motion accepted in its burst.
  os.kill(p.pid,signal.SIGSTOP)
  try:ctl.s.sendall(packet(17,x+60,y)+ctl.encode('input_button',dict(x=x+60,y=y,pressed=False)))
  finally:os.kill(p.pid,signal.SIGCONT)
  released=wire.read(ctl.f);assert released['ok'] and released['id']==ctl.seq,released
  ctl.call('wait_present',dict(generation=released['result']['generation']))
  assert not ctl.call('input_status')['pressed'] and ctl.call('get_state')['windows'][0]['azimuth']!=0
  ctl.do('configure_window',dict(window_id=wid,azimuth=0,elevation=0,width_deg=20,height_deg=14))
  h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='resize');x,y=h['x'],h['y']
  ctl.do('input_button',dict(x=x,y=y,pressed=True));base=ctl.call('get_state')['performance']['last_render']
  os.kill(p.pid,signal.SIGSTOP)
  try:ctl.s.sendall(b''.join(packet(17+i,x+i*2,y+i) for i in range(1,17)))
  finally:os.kill(p.pid,signal.SIGCONT)
  progress=ctl.call('input_progress');done=ctl.call('wait_input',dict(ticket=progress['accepted_ticket']))
  state=ctl.call('input_status');assert state['preview']['mode']=='tile-rim' and state['preview']['frozen'] and done['fast_visible_sequence']==33
  assert len(state['preview']['rim_tiles'])>4 and state['preview']['payload_ops']==0 and state['preview']['payload_builds']==0
  after=ctl.call('get_state')['performance']['last_render']
  for field in ['plan_builds_total','run_submissions_total']:assert base[field]==after[field],field
  target=state['preview']['target'];ctl.do('input_button',dict(x=x+32,y=y+16,pressed=False))
  window=ctl.call('get_state')['windows'][0]
  for key in ['azimuth','elevation','width_deg','height_deg']:assert window[key]==target[key],(key,window,target)
  for target,raw in [(path,packet(1,10,10)),(path+'.control',packet(0,10,10)),(path+'.control',packet(1,1280,0)),(path+'.control',struct.pack('<III',0x314d5742,4,0x31525450))]:
   bad=Client(target);bad.s.sendall(raw);assert bad.s.recv(1)==b'';bad.close()
  assert ctl.call('input_status')['x']==x+32
  ctl.close();app.close();print('PASS_FAST_MOTION: latest-only, fragmented packets, no ACKs, stale suppression, button ordering, capture ownership, invalid packet isolation, no projection')
 finally:
  p.terminate();p.wait(timeout=15);assert p.returncode==0,p.returncode
