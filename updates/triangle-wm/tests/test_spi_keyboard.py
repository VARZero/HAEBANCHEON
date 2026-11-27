"""SPI syscall contract/ACK plus Linux input_event records; no SPI hardware."""
import mmap,os,struct,subprocess,sys,tempfile,time,zlib
from example_test_support import Control,until
wm,bridge,preload=map(os.path.abspath,sys.argv[1:4])
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';fixture=folder+'/spi';fifo=folder+'/events';env={**os.environ,'BOAYO_SOCKET':path};log=open(folder+'/log','w+')
 open(fixture,'wb').write(bytes(40));os.mkfifo(fifo);writer=os.open(fifo,os.O_RDWR|os.O_NONBLOCK)
 server=subprocess.Popen([wm,'--mock'],env=env,stdout=log,stderr=log);listener=None;clients=[];pixels=None
 try:
  until(lambda:os.path.exists(path+'.control'));app=Control(path);ctl=Control(path+'.control');clients=[app,ctl]
  wid=app.do('create_window',dict(surface_width=100,surface_height=100,width_deg=20,height_deg=14))['window_id']
  info=app.call('create_surface',dict(window_id=wid,format='RGB332'));fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd);pixels[:10000]=bytes([36])*10000
  app.do('commit_surface',dict(window_id=wid,sequence=1,slot=0,damage=[[0,0,100,100]]))
  app.call('set_focusables',dict(window_id=wid,targets=[dict(id='a',x=5,y=5,w=20,h=20),dict(id='b',x=65,y=5,w=20,h=20)]));app.call('poll_events')
  listener=subprocess.Popen([bridge,'--spi',fixture,'--event',fifo],env={**env,'LD_PRELOAD':preload,'KEYBOARD_SPI_FIXTURE':fixture},stdout=log,stderr=log)
  def frame(session,sequence,key,value=1,corrupt=False):
   data=struct.pack('<4sIIBBH',b'KBD1',session,sequence,key,value,0);data+=struct.pack('<I',zlib.crc32(data)^(1 if corrupt else 0))
   with open(fixture,'r+b',buffering=0) as f:f.write(data)
  def ack(session,sequence):
   with open(fixture,'rb',buffering=0) as f:f.seek(20);data=f.read(20)
   return len(data)==20 and data[:4]==b'KACK' and struct.unpack_from('<II',data,4)==(session,sequence) and zlib.crc32(data[:16])==struct.unpack_from('<I',data,16)[0]
  frame(91,1,3);until(lambda:ack(91,1));assert ctl.call('navigation_status')['target']=='b'
  time.sleep(.15);assert ctl.call('navigation_status')['target']=='b';app.call('poll_events')
  frame(91,2,6);until(lambda:ack(91,2));time.sleep(.1);events=app.call('poll_events')['events'];assert len([e for e in events if e['type']=='click'])==1,events
  frame(91,3,6,2);until(lambda:ack(91,3));assert not app.call('poll_events')['events'],'held Enter activated twice'
  frame(91,4,2,corrupt=True);time.sleep(.15);assert not ack(91,4) and ctl.call('navigation_status')['target']=='b'
  frame(92,1,2);until(lambda:ack(92,1));assert ctl.call('navigation_status')['target']=='a','ESP32 boot session was not distinguished'
  # Stop sending SPI events; retain the last ACK. Feed actual native input_event layout.
  def event(type,code,value):os.write(writer,struct.pack('@llHHi',0,0,type,code,value))
  event(1,106,1);until(lambda:ctl.call('navigation_status')['target']=='b')
  event(1,105,0);event(1,28,2);time.sleep(.1);assert ctl.call('navigation_status')['target']=='b';assert not [e for e in app.call('poll_events')['events'] if e['type']=='click']
  event(0,3,0);event(1,103,1);time.sleep(.1);assert ctl.call('navigation_status')['target']=='b';event(0,0,0);event(1,103,1);until(lambda:ctl.call('navigation_status')['target']=='$close')
  print('PASS_SPI_KEYBOARD CRC ACK retransmit Enter-repeat reboot-session evdev-release SYN_DROPPED')
 finally:
  if listener and listener.poll() is None:listener.terminate();listener.wait(timeout=5)
  if pixels is not None:pixels.close()
  for c in clients:c.close()
  if server.poll() is None:server.terminate();server.wait(timeout=5)
  os.close(writer);log.close()
