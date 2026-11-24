"""Continuous latest-only drag, timed to actual HDMI arrival of a distinct last tile."""
import argparse,bisect,json,statistics,threading,time,subprocess
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--hz',type=float,default=10);ap.add_argument('--count',type=int,default=20);a=ap.parse_args();assert 1<=a.hz<=120 and 3<=a.count<=1000
a.output.mkdir(parents=True,exist_ok=True);url='http://127.0.0.1:8767'
def call(op,**args):
 t=time.monotonic();q=Request(url+'/input',data=json.dumps(dict(op=op,args=args)).encode(),headers={'Origin':url,'Content-Type':'application/json'})
 with urlopen(q,timeout=60) as f:r=json.load(f)
 assert r.get('ok'),r
 return dict(start=t,end=time.monotonic(),reply=r)
initial=call('input_status')['reply']['result'];assert not initial['pressed']
pref=initial['display_enabled'];rounds=[]
call('input_cancel',hide=True);call('input_visibility',visible=False)
try:
 for trial in range(3):
  state=call('input_status')['reply']['result'];handle=next(c for c in state['controls'] if c['action']=='move')
  x,y=handle['x'],handle['y'];target=(min(1250,x+311),max(30,y-131))
  # Current benchmark scene is centered at yaw/pitch zero. Projection center is
  # 639.5/359.5; the marker follows the press-relative camera-plane displacement.
  endpoint=(round(639.5+target[0]-x),round(359.5+target[1]-y))
  press=call('input_button',x=x,y=y,pressed=True)
  folder=a.output/str(trial);folder.mkdir(exist_ok=True)
  frames=[];sent=[];events=[];errors=[];condition=threading.Condition();pending=[None];finished=[False];stop=threading.Event();ready=threading.Event()
  def capture():
   try:
    while not stop.is_set():
     t=time.monotonic()
     with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
     observed=time.monotonic();im=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
     px,py=endpoint;roi=im[py-5:py+6,px-5:px+6].astype(np.int16)
     red=int(np.count_nonzero((roi[:,:,2]>150)&(roi[:,:,1]<110)&(roi[:,:,0]<100)))
     name=f'{len(frames):05d}.jpg';(folder/name).write_bytes(raw)
     frames.append(dict(time=observed,file=name,endpoint_red_pixels=red));ready.set()
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
   assert frames[0]['endpoint_red_pixels']<10,'Endpoint already colored; scene must start centered.'
   work=threading.Thread(target=worker);work.start();start=time.monotonic()
   for i in range(a.count):
    time.sleep(max(0,start+i/a.hz-time.monotonic()))
    ex=round(x-289+360*i/(a.count-2)) if i<a.count-1 else target[0]
    event=dict(index=i,time=time.monotonic(),x=max(0,min(1279,ex)),y=target[1]);events.append(event)
    with condition:pending[0]=event;condition.notify()
   with condition:finished[0]=True;condition.notify()
   work.join(60);assert not work.is_alive() and not errors,errors
   assert sent[-1]['event']['index']==a.count-1
   deadline=time.monotonic()+12
   while not any(f['time']>=events[-1]['time'] and f['endpoint_red_pixels']>=20 for f in frames):
    assert time.monotonic()<deadline,'Last marker absent on HDMI'
    time.sleep(.05)
   time.sleep(.3)
  finally:
   with condition:finished[0]=True;condition.notify()
   if work:work.join(60)
   stop.set();camera.join(10)
  assert not errors,errors
  # The final jump goes to a separate tile. Earlier path points cannot have
  # satisfied the endpoint; avoid reporting zero latency within the same tile.
  assert all(f['endpoint_red_pixels']<20 for f in frames if f['time']<events[-1]['time'])
  observed=next(f for f in frames if f['time']>=events[-1]['time'] and f['endpoint_red_pixels']>=20)
  progress=call('input_progress');state=call('input_status')['reply']['result']
  assert (state['x'],state['y'])==target and state['preview']['active'] and state['preview']['colored_cells_per_tile']==1024
  report=dict(trial=trial,source_hz=a.hz,source_events=len(events),transmitted=len(sent),coalesced=a.count-len(sent),endpoint=endpoint,
              final_event_to_hdmi_ms=(observed['time']-events[-1]['time'])*1000,press=press,events=events,sent=sent,frames=frames,progress=progress)
  rounds.append(report);(folder/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
  print('LIVE_MARKER',json.dumps({k:report[k] for k in ['trial','transmitted','coalesced','final_event_to_hdmi_ms']}),flush=True)
  call('input_cancel',hide=True)
finally:
 call('input_cancel',hide=True);call('input_visibility',visible=pref)
result=dict(scope='Physical board, configurable source (see rounds), one HTTP request in flight and newest queued coordinate only. Final distinct tile arrival in actual USB HDMI capture. Includes HTTP/UART/USB capture sampling; not optical timing. Scene yaw/pitch and view remain zero.',hardware=True,rounds=rounds,
            median_event_to_hdmi_ms=statistics.median(r['final_event_to_hdmi_ms'] for r in rounds))
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print('LIVE_MARKER_DONE',result['median_event_to_hdmi_ms'],flush=True)
