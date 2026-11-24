"""Cached fast resize must match the original formula and pixels exactly."""
import os,socket,struct,subprocess,sys,tempfile,mmap
import binary_test_wire as wire
class Peer:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path);self.f=self.s.makefile('rb');self.seq=0
 def call(self,op,args=None):
  self.seq+=1;self.s.sendall(wire.encode(dict(id=self.seq,op=op,args=args or {})));r=wire.read(self.f);assert r['ok'],r;return r['result']
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
  return r
 def close(self):self.f.close();self.s.close()
with tempfile.TemporaryDirectory() as folder:
 servers=[];pairs=[];seq=0
 try:
  for i,binary in enumerate(sys.argv[1:3]):
   path=folder+'/'+str(i);p=subprocess.Popen([binary,'--mock'],env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary'},stdout=subprocess.PIPE,text=True);servers.append(p)
   assert p.stdout.readline().startswith('TRIANGLE_BOAYO_READY');app,ctl=Peer(path),Peer(path+'.control');pairs.append((app,ctl))
   app.do('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32))
   info=app.call('create_surface',dict(window_id=1,format='RGB332'));fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd)
   pixels[:1024]=bytes(range(256))*4;app.do('commit_surface',dict(window_id=1,slot=0,sequence=1,damage=[[0,0,32,32]]));pixels.close()
   ctl.do('input_visibility',dict(visible=False))
  for yaw,pitch,width,height in [(0,0,20,14),(5,3,24,18),(-6,-4,18,12)]:
   handles=[]
   for app,ctl in pairs:
    ctl.do('configure_window',dict(window_id=1,azimuth=yaw,elevation=pitch,width_deg=width,height_deg=height))
    h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='resize');handles.append(h)
    ctl.do('input_button',dict(x=h['x'],y=h['y'],pressed=True))
   assert handles[0]==handles[1],handles
   h=handles[0]
   for dx,dy in [(0,0),(1,1),(31,12),(70,35),(-50,-25),(110,75),(70,35)]:
    x=max(0,min(1279,h['x']+dx));y=max(0,min(719,h['y']+dy))
    pairs[0][1].do('input_move',dict(x=x,y=y))
    seq+=1;c=pairs[1][1];c.s.sendall(struct.pack('<IIIIHH',0x314d5742,12,0x31525450,seq,x,y));accepted=c.call('input_progress');c.call('wait_input',dict(ticket=accepted['accepted_ticket']))
    statuses=[c.call('input_status')['preview'] for a,c in pairs]
    for key in ['target','rim_tiles','visible_rim_tiles']:assert statuses[0][key]==statuses[1][key],(yaw,pitch,dx,dy,key,statuses)
    assert pairs[0][1].call('snapshot')['tile_hashes']==pairs[1][1].call('snapshot')['tile_hashes'],(yaw,pitch,dx,dy)
   for a,c in pairs:c.do('input_button',dict(x=x,y=y,pressed=False))
   windows=[c.call('get_state')['windows'][0] for a,c in pairs]
   for key in ['azimuth','elevation','width_deg','height_deg']:assert windows[0][key]==windows[1][key],windows
  print('PASS_RESIZE_REFERENCE: original formula vs cached fast path; grow, shrink, repeated coordinates, 3 poses, exact target/rim/pixels and release')
 finally:
  for app,ctl in pairs:ctl.close();app.close()
  for p in servers:p.terminate();p.wait(timeout=15);assert p.returncode==0,p.returncode
