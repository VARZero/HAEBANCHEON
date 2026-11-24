"""Continuous latest-only drag, timed to actual HDMI arrival of a distinct final resize rim."""
import argparse,bisect,json,statistics,threading,time,subprocess
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--probe-from',type=Path);ap.add_argument('--hz',type=float,default=10);ap.add_argument('--count',type=int,default=20);ap.add_argument('--preview-mode',choices=['tile-rim','solid-resize'],default='tile-rim');a=ap.parse_args();assert 1<=a.hz<=120 and 3<=a.count<=1000
a.output.mkdir(parents=True,exist_ok=True);url='http://127.0.0.1:8767'
def call(op,**args):
 t=time.monotonic();q=Request(url+'/input',data=json.dumps(dict(op=op,args=args)).encode(),headers={'Origin':url,'Content-Type':'application/json'})
 with urlopen(q,timeout=60) as f:r=json.load(f)
 assert r.get('ok'),r
 return dict(start=t,end=time.monotonic(),reply=r)
initial=call('input_status')['reply']['result'];assert not initial['pressed']
pref=initial['display_enabled'];rounds=[]
call('input_cancel',hide=True);call('input_visibility',visible=False)
def settle(r):
 seq=r['reply']['result']['queued_sequence'];deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  p=call('input_progress')['reply']['result']
  if ((p['fast_visible_sequence']-seq)&0xffffffff)<0x80000000:return p
  time.sleep(.05)
 raise TimeoutError('resize did not publish')
def shot():
 with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
 return cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
def red(im):return (im[:,:,2]>150)&(im[:,:,1]<110)&(im[:,:,0]<100)
try:
 # Calibrate a final-only area of the outer rim before starting any timed input.
 state=call('input_status')['reply']['result'];h=next(c for c in state['controls'] if c['action']=='resize');cx,cy=h['x'],h['y']
 call('input_button',x=cx,y=cy,pressed=True);initial_geometry=call('input_status')['reply']['result']['preview']['target'];time.sleep(.2);initial_red=red(shot())
 settle(call('input_move',x=min(1279,cx+40),y=min(719,cy+20)));time.sleep(.25);middle=red(shot())
 settle(call('input_move',x=min(1279,cx+120),y=min(719,cy+70)));time.sleep(.25);goal=shot();goal_red=red(goal)
 target_state=call('input_status')['reply']['result']['preview'];assert target_state['mode']==a.preview_mode
 if a.probe_from:
  reference=json.loads((a.probe_from/'calibration.json').read_text());assert initial_geometry==reference['initial_geometry'];assert target_state['target']==reference['target'],(target_state['target'],reference['target']);assert set(reference['rim_tiles'])<=set(target_state.get('filled_tiles',target_state.get('rim_tiles',[])))
  probe=np.load(a.probe_from/'probe.npy');deadline=time.monotonic()+3
  while int((goal_red&probe).sum())<int(probe.sum())*.9:
   cv2.imwrite(str(a.output/'calibration-observed.jpg'),goal)
   assert time.monotonic()<deadline,'Reference goal rim absent from capture'
   time.sleep(.05);goal=shot();goal_red=red(goal)
 else:
  occupied=initial_red|middle;edge=int(np.where(occupied)[1].max());probe=goal_red.copy();probe[:,:edge+12]=False
 assert int(probe.sum())>100,'Need a larger distinct final rim region'
 np.save(a.output/'probe.npy',probe);cv2.imwrite(str(a.output/'goal.jpg'),goal)
 (a.output/'calibration.json').write_text(json.dumps(dict(initial_geometry=initial_geometry,target=target_state['target'],rim_tiles=target_state.get('filled_tiles',target_state.get('rim_tiles',[])),preview_mode=a.preview_mode,probe_pixels=int(probe.sum()),final_delta=[120,70],path_delta=[[-30,-15],[40,20]]),indent=2)+'\n')
 threshold=int(probe.sum()*.6);call('input_cancel',hide=True)

 for trial in range(3):
  state=call('input_status')['reply']['result'];handle=next(c for c in state['controls'] if c['action']=='resize')
  x,y=handle['x'],handle['y'];target=(min(1279,x+120),min(719,y+70))
  endpoint=None
  press=call('input_button',x=x,y=y,pressed=True)
  folder=a.output/str(trial);folder.mkdir(exist_ok=True)
  frames=[];sent=[];events=[];errors=[];condition=threading.Condition();pending=[None];finished=[False];stop=threading.Event();ready=threading.Event()
  def capture():
   try:
    while not stop.is_set():
     t=time.monotonic()
     with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
     observed=time.monotonic();im=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
     red_pixels=int((red(im)&probe).sum())
     name=f'{len(frames):05d}.jpg';(folder/name).write_bytes(raw)
     frames.append(dict(time=observed,file=name,endpoint_red_pixels=red_pixels));ready.set()
     stop.wait(max(0,1/30-(time.monotonic()-t)))
   except Exception as e:errors.append(str(e));ready.set()
  def worker():
   try:
    while True:
     with condition:
      condition.wait_for(lambda:pending[0] is not None or finished[0])
      if pending[0] is None:return
      event=pending[0];pending[0]=None
     result=call('input_move',x=event['x'],y=event['y']);result['event']=event;sent.append(result)
   except Exception as e:errors.append(str(e))
  camera=threading.Thread(target=capture);camera.start();work=None
  try:
   assert ready.wait(10) and not errors,errors
   assert frames[0]['endpoint_red_pixels']<threshold,'Final rim probe already colored'
   work=threading.Thread(target=worker);work.start();start=time.monotonic()
   for i in range(a.count):
    time.sleep(max(0,start+i/a.hz-time.monotonic()))
    phase=i/(a.count-2);delta=round(-30+70*(1-abs(2*phase-1))) if i<a.count-1 else 120
    ex=x+delta;ey=y+round(delta/2) if i<a.count-1 else target[1]
    event=dict(index=i,time=time.monotonic(),x=max(0,min(1279,ex)),y=max(0,min(719,ey)));events.append(event)
    with condition:pending[0]=event;condition.notify()
   with condition:finished[0]=True;condition.notify()
   work.join(60);assert not work.is_alive() and not errors,errors
   assert sent[-1]['event']['index']==a.count-1
   deadline=time.monotonic()+12
   while not any(f['time']>=events[-1]['time'] and f['endpoint_red_pixels']>=threshold for f in frames):
    assert time.monotonic()<deadline,'Final resize rim absent on HDMI'
    time.sleep(.05)
   time.sleep(.3)
  finally:
   with condition:finished[0]=True;condition.notify()
   if work:work.join(60)
   stop.set();camera.join(10)
  assert not errors,errors
  # The final jump goes to a separate tile. Earlier path points cannot have
  # satisfied the endpoint; avoid reporting zero latency within the same tile.
  assert all(f['endpoint_red_pixels']<threshold for f in frames if f['time']<events[-1]['time'])
  observed=next(f for f in frames if f['time']>=events[-1]['time'] and f['endpoint_red_pixels']>=threshold)
  progress=call('input_progress');state=call('input_status')['reply']['result']
  assert (state['x'],state['y'])==target and state['preview']['active'] and state['preview']['colored_cells_per_tile']==1024 and state['preview']['mode']==a.preview_mode and state['preview']['target']==target_state['target']
  report=dict(trial=trial,source_hz=a.hz,source_events=len(events),transmitted=len(sent),coalesced=a.count-len(sent),endpoint=endpoint,probe_pixels=int(probe.sum()),detection_threshold=threshold,preview=state['preview'],
              final_event_to_hdmi_ms=(observed['time']-events[-1]['time'])*1000,press=press,events=events,sent=sent,frames=frames,progress=progress)
  rounds.append(report);(folder/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
  print('LIVE_RESIZE',json.dumps({k:report[k] for k in ['trial','transmitted','coalesced','final_event_to_hdmi_ms']}),flush=True)
  call('input_cancel',hide=True)
finally:
 call('input_cancel',hide=True);call('input_visibility',visible=pref)
result=dict(scope='Physical board, configurable source (see rounds), one HTTP request in flight and newest queued coordinate only. Final distinct outer-rim area arrival in actual USB HDMI capture. Includes HTTP/UART/USB capture sampling; not optical timing. Scene yaw/pitch and view remain zero.',hardware=True,rounds=rounds,
            median_event_to_hdmi_ms=statistics.median(r['final_event_to_hdmi_ms'] for r in rounds))
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print('LIVE_RESIZE_DONE',result['median_event_to_hdmi_ms'],flush=True)
