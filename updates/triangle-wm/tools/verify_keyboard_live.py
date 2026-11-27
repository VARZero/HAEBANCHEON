"""Actual UART -> captured HDMI tests. Capture buffering is included, not optical latency."""
import argparse,json,threading,time
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--settings-ready',action='store_true');a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
base='http://127.0.0.1:8770';samples=[];stop=threading.Event();phase='Home launcher';video=[];started=time.monotonic()
def post(path,data,timeout=100):
 q=Request(base+path,data=json.dumps(data).encode(),headers={'Origin':base,'Content-Type':'application/json'})
 with urlopen(q,timeout=timeout) as f:return json.load(f)
def status():
 with urlopen(base+'/status',timeout=5) as f:return json.load(f)
def frame():
 with urlopen(base+'/frame',timeout=5) as f:raw=f.read()
 image=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR);assert image is not None and np.count_nonzero(image.max(axis=2)>12)>1000,'HDMI frame is black';return raw,image
def record():
 writer=cv2.VideoWriter(str(a.output/'keyboard-demo.mp4'),cv2.VideoWriter_fourcc(*'mp4v'),10,(1280,720))
 try:
  while not stop.is_set():
   t=time.monotonic();_,image=frame();cv2.rectangle(image,(0,672),(1280,720),(15,22,32),-1);cv2.putText(image,f'{t-started:7.2f}s  {phase}',(18,703),cv2.FONT_HERSHEY_SIMPLEX,.65,(255,255,255),1,cv2.LINE_AA);writer.write(image);video.append({'seconds':t-started,'phase':phase});stop.wait(max(0,.1-(time.monotonic()-t)))
 finally:writer.release()
def wait_mode(value):
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  if status()['mode']==value:return
  time.sleep(.1)
 raise TimeoutError('UART mode did not become '+value)
def key(name,key,roi=None,threshold=25):
 global phase
 phase=name;time.sleep(.6);raw,before=frame();(a.output/(name+'-before.jpg')).write_bytes(raw)
 reply=post('/key',{'key':key});sent=reply['host_monotonic'];deadline=sent+20;first=None
 while time.monotonic()<deadline:
  raw,image=frame();difference=np.max(cv2.absdiff(before,image),axis=2)>35
  if roi:
   x0,y0,x1,y1=roi;difference=difference[y0:y1,x0:x1]
  changed=int(np.count_nonzero(difference))
  if changed>=threshold:first=time.monotonic();break
  time.sleep(.03)
 if first is None:raise TimeoutError(name+' showed no HDMI change')
 (a.output/(name+'-after.jpg')).write_bytes(raw);sample={'name':name,'key':key,'host_to_first_capture_change_ms':round((first-sent)*1000,2),'changed_pixels':changed,'roi':roi};samples.append(sample);(a.output/'samples.json').write_text(json.dumps(samples,indent=2)+'\n');print(json.dumps(sample),flush=True);return sent
thread=threading.Thread(target=record);thread.start()
try:
 wait_mode('tty')
 close_roi=(590,80,730,165)
 if not a.settings_ready:
  # Start with the captionless Home launcher focused at its first target.
  key('01-select-dashboard-icon','ArrowDown',None,100)
  sent=key('02-launch-dashboard','Enter',(305,185,570,222),200)
  phase='Wait for Dashboard content';deadline=time.monotonic()+45
  while True:
   raw,image=frame();white=np.min(image[185:222,448:570],axis=2)>150
   if np.count_nonzero(white)>130:break
   if time.monotonic()>deadline:raise TimeoutError('Dashboard content not observed')
   time.sleep(.05)
  samples.append({'name':'03-dashboard-content','key':'Enter','host_to_content_ms':round((time.monotonic()-sent)*1000,2)})
  key('04-focus-close','ArrowUp',close_roi)
  key('05-focus-content','ArrowDown',close_roi)
  key('06-focus-close-again','ArrowUp',close_roi)
  key('07-close-dashboard','Enter',(305,185,570,222),200)
  phase='Console - launch Settings';post('/tty',{'enabled':False});wait_mode('shell')
  command='nice -n 10 /run/triangle-wm-runtime/triangle-examples settings >/tmp/keyboard-settings.log 2>&1 & echo $! >/run/keyboard-settings.pid'
  result=post('/command',{'command':command,'timeout':60});assert result['rc']==0
  ready='i=0; while ! grep -q EXAMPLE_READY /tmp/keyboard-settings.log && test "$i" -lt 30; do i=$((i+1)); sleep 1; done; tail -2 /tmp/keyboard-settings.log'
  result=post('/command',{'command':ready,'timeout':150},timeout=155);assert 'EXAMPLE_READY' in result['output'],result
 if not a.settings_ready:post('/tty',{'enabled':True});wait_mode('tty')
 for name,value in [('09-splash-right','ArrowRight'),('10-splash-right','ArrowRight'),('11-splash-left','ArrowLeft'),('12-background-down','ArrowDown'),('13-background-right','ArrowRight'),('14-enter-select-background','Enter')]:key(name,value)
 phase='Console - verify selected component';post('/tty',{'enabled':False});wait_mode('shell')
 result=post('/command',{'command':"/run/triangle-wm-runtime/triangle-boayo-fixed --ctl '{\"op\":\"navigation_status\"}'",'timeout':60});assert result['rc']==0 and 'background2' in result['output'],result
 (a.output/'final-navigation.json').write_text(json.dumps(result,indent=2)+'\n')
 post('/tty',{'enabled':True});wait_mode('tty');phase='Ready - interactive UART keys';time.sleep(.5)
 report={'result':'PASS_REAL_BOARD_UART_KEYBOARD_HDMI','scope':'Actual COM4 UART ANSI -> foreground boayo-input -> WM -> observed USB HDMI frame. Includes capture buffering and polling, not optical latency or isolated CPU/driver time. HDMI recovered after user reconnected cable.','samples':samples,'final_component':'background2','live_url':base,'input_left_running':True,'video':'keyboard-demo.mp4','settings_only':a.settings_ready}
 (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
finally:
 stop.set();thread.join(timeout=10);(a.output/'video-timeline.json').write_text(json.dumps(video,indent=2)+'\n')
