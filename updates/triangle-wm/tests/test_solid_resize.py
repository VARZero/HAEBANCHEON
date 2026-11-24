"""Filled resize hides content, restores shrink/cancel, and commits latest bytes."""
import mmap,os,socket,subprocess,sys,tempfile
import binary_test_wire as wire
class Client:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path);self.f=self.s.makefile('rb');self.seq=0
 def call(self,op,args=None,ok=True):
  self.seq+=1;self.s.sendall(wire.encode(dict(id=self.seq,op=op,args=args or {})));r=wire.read(self.f);assert r['ok']==ok,r;return r.get('result',r)
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
  return r
 def close(self):self.f.close();self.s.close()
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';p=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_INPUT':'1','TRIANGLE_PREVIEW':'tile'},stdout=subprocess.PIPE,text=True)
 assert p.stdout.readline().startswith('TRIANGLE_BOAYO_READY');app=Client(path);ctl=Client(path+'.control');pixels=None
 try:
  ctl.do('input_visibility',dict(visible=False));ctl.do('input_cancel',dict(hide=True))
  wid=app.do('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32,background_index=93,splash_index=201))['window_id']
  ctl.do('input_colors',dict(window_id=wid,background_index=91,splash_index=202))
  colors=ctl.call('input_status')['window_colors'];assert colors==[dict(window_id=wid,background_index=91,splash_index=202)]
  before=ctl.call('snapshot')['tile_hashes']
  ctl.call('input_colors',dict(window_id=wid,background_index=300,splash_index=1),ok=False)
  assert ctl.call('input_status')['window_colors']==colors and ctl.call('snapshot')['tile_hashes']==before
  info=app.call('create_surface',dict(window_id=wid,format='RGB332'));fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd)
  pixels[:1024]=bytes(range(256))*4
  app.do('commit_surface',dict(window_id=wid,slot=0,sequence=1,damage=[[0,0,32,32]]));original=ctl.call('snapshot')['tile_hashes']
  h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='resize');x,y=h['x'],h['y']
  ctl.do('input_button',dict(x=x,y=y,pressed=True));preview=ctl.call('input_status')['preview']
  assert preview['mode']=='solid-resize' and preview['payload_ops']==0
  # Every target tile, including the old content center, is the same S payload.
  s_hash=2166136261
  for _ in range(1024):s_hash=((s_hash^202)*16777619)&0xffffffff
  hashes=ctl.call('snapshot')['tile_hashes'];assert all(hashes[t]==s_hash for t in preview['visible_filled_tiles'])
  assert all(c['window_id']!=wid for c in ctl.call('input_status')['controls'])
  ctl.call('input_colors',dict(window_id=wid,background_index=5),ok=False);assert ctl.call('input_status')['preview']['active']
  ctl.do('input_move',dict(x=x-100,y=y-60));smaller=ctl.call('input_status')['preview'];assert len(smaller['filled_tiles'])<len(preview['filled_tiles'])
  zero_hash=2166136261
  for _ in range(1024):zero_hash=(zero_hash*16777619)&0xffffffff
  hashes=ctl.call('snapshot')['tile_hashes'];remaining=set(smaller['visible_filled_tiles'])
  assert all(hashes[t]==zero_hash for t in set(preview['visible_filled_tiles'])-remaining)
  ctl.do('input_cancel',dict(hide=True));assert ctl.call('snapshot')['tile_hashes']==original
  # Latest application damage remains hidden during resize; one release publishes it.
  h=next(c for c in ctl.call('input_status')['controls'] if c['action']=='resize');x,y=h['x'],h['y'];ctl.do('input_button',dict(x=x,y=y,pressed=True));frozen=ctl.call('snapshot')['tile_hashes']
  for seq,color in [(2,9),(3,111),(4,219)]:
   pixels[:1024]=bytes([color])*1024;app.call('commit_surface',dict(window_id=wid,slot=0,sequence=seq,damage=[[0,0,32,32]]));assert ctl.call('snapshot')['tile_hashes']==frozen
  ctl.do('input_button',dict(x=x+60,y=y+30,pressed=False));assert not ctl.call('input_status')['preview']['active']
  committed=ctl.call('snapshot')['tile_hashes'];ctl.do('configure_window',dict(window_id=wid,azimuth=0,elevation=0,width_deg=20,height_deg=14));assert ctl.call('snapshot')['tile_hashes']!=original
  assert ctl.call('input_status')['window_colors']==colors
  print('PASS_SOLID_RESIZE: arbitrary RGB332 S/B, atomic validation, filled blank content, shrink removal, cancel, latest updates and release')
 finally:
  if pixels:pixels.close()
  ctl.call('shutdown');app.close();ctl.close();assert p.wait(timeout=15)==0
