"""Exercise the physical preview through the existing bridge, saving timed HDMI."""
import argparse,bisect,json,threading,time,subprocess
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
a.output.mkdir(parents=True,exist_ok=True);(a.output/'frames').mkdir(exist_ok=True)
base='http://127.0.0.1:8767';events=[];frames=[];errors=[];stop=threading.Event();phase='Preparing';start=time.monotonic()
def call(op,**args):
 global phase
 t=time.monotonic();req=Request(base+'/input',data=json.dumps(dict(op=op,args=args)).encode(),headers={'Origin':base,'Content-Type':'application/json'})
 with urlopen(req,timeout=60) as f:r=json.load(f)
 assert r.get('ok'),r
 events.append(dict(op=op,args=args,phase=phase,start=t,end=time.monotonic(),reply=r))
 return r
def frame(name):
 with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
 (a.output/(name+'.jpg')).write_bytes(raw)
 return cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
def record():
 try:
  while not stop.is_set():
   t=time.monotonic()
   with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
   name=f'{len(frames):05d}.jpg';(a.output/'frames'/name).write_bytes(raw)
   frames.append(dict(time=time.monotonic(),file=name,phase=phase))
   stop.wait(max(0,.1-(time.monotonic()-t)))
 except Exception as e:errors.append(str(e))
def wait(r):
 seq=r['result'].get('queued_sequence');ticket=r['result'].get('accepted_ticket')
 deadline=time.monotonic()+30
 while time.monotonic()<deadline:
  p=call('input_progress')['result']
  if (seq is not None and ((p['fast_visible_sequence']-seq)&0xffffffff)<0x80000000) or (seq is None and p['visible_ticket']>=ticket):return p
  time.sleep(.05)
 raise TimeoutError('latest pointer not completed')
def bbox(im):
 mask=(im[:,:,0]>80)&(im[:,:,0]>im[:,:,1]+40)&(im[:,:,0].astype(int)>im[:,:,2].astype(int)+25)
 y,x=np.where(mask);assert len(x)>10000
 return [int(x.min()),int(y.min()),int(x.max()),int(y.max())]
initial=call('input_status')['result'];assert initial['preview']['mode'] in ['single-tile','tile-rim']
call('input_cancel',hide=True);call('input_visibility',visible=False)
thread=threading.Thread(target=record);thread.start();actions=[]
try:
 for kind in ['move','resize']:
  phase=kind.upper()+' - locating control'
  state=call('input_status')['result']
  handle=next(c for c in state['controls'] if c['action']==kind)
  x,y=handle['x'],handle['y']
  before=frame(kind+'-before')
  phase=kind.upper()+' - press / freeze'
  press=call('input_button',x=x,y=y,pressed=True)
  time.sleep(.2);frozen=frame(kind+'-press')
  phase=kind.upper()+(' - representative tile' if kind=='move' else ' - target preview')
  target=(min(1240,x+(120 if kind=='move' else 55)),max(30,min(680,y+(-25 if kind=='move' else 35))))
  accepted=call('input_move',x=target[0],y=target[1]);completion=wait(accepted)
  state=call('input_status')['result']
  assert state['preview']['active'] and state['preview']['frozen'] and state['captured_window']==handle['window_id']
  time.sleep(.25);preview=frame(kind+'-preview')
  frozen_box,preview_box=bbox(frozen),bbox(preview)
  rim=state['preview']['mode']=='tile-rim'
  full_move=not rim and state['preview'].get('colored_cells_per_tile')==1024
  if rim:assert len(state['preview']['rim_tiles'])>4 and state['preview']['payload_ops']==0
  elif full_move:assert state['preview']['payload_ops']==0
  else:assert max(abs(a-b) for a,b in zip(frozen_box,preview_box))<=3,(frozen_box,preview_box)
  # Also compare against pre-press static labels, not only already-frozen frames.
  x0,y0,x1,y1=bbox(before);w,h=x1-x0,y1-y0
  region=(slice(int(y0+.2*h),int(y0+.6*h)),slice(int(x0+.18*w),int(x0+.74*w)))
  title_white=[int(np.all(im[region]>170,axis=2).sum()) for im in [before,frozen,preview]]
  assert title_white[0]>1000,title_white
  # Full boundary tiles may intentionally cover old text. Compare the remaining
  # static-label pixels outside both S-colored overlays, including JPEG edges.
  def red(im):return ((im[:,:,2]>150)&(im[:,:,1]<100)&(im[:,:,0]<100)).astype(np.uint8)
  cover=cv2.dilate(red(frozen)|red(preview),np.ones((7,7),np.uint8))
  protected=np.all(before[region]>170,axis=2)&(cover[region]==0)
  assert int(protected.sum())>500
  for im in [frozen,preview]:assert int((np.all(im[region]>170,axis=2)&protected).sum())>=protected.sum()*.9
  # Explicit demonstration hold, not input latency.
  phase=kind.upper()+' - intentional hold (2 s)'
  time.sleep(2);held=frame(kind+'-held')
  frozen_content=np.all(frozen[240:485,405:790]>170,axis=2)
  held_content=np.all(held[240:485,405:790]>170,axis=2)
  cover=cv2.dilate(red(frozen)|red(held),np.ones((7,7),np.uint8))
  changed=int(np.count_nonzero((frozen_content!=held_content)&(cover[240:485,405:790]==0)))
  assert changed<max(100,int(frozen_content.sum()*.03)),changed
  phase=kind.upper()+' - release / real transform'
  released=call('input_button',x=target[0],y=target[1],pressed=False)
  assert released['result']['transform']['state']=='complete'
  time.sleep(.3);after=frame(kind+'-after')
  after_box=bbox(after);assert max(abs(a-b) for a,b in zip(preview_box,after_box))>8,(preview_box,after_box)
  state=call('input_status')['result']
  assert not state['preview']['active'] and not state['pressed'] and state['captured_window']==0
  actions.append(dict(action=kind,preview_mode=state['preview']['mode'] if not rim else 'tile-rim',rim_tiles=state['preview'].get('rim_tiles',[]) if rim else [],press=press,move=accepted,completion=completion,release=released,before_bbox=bbox(before),frozen_bbox=frozen_box,preview_bbox=preview_box,after_bbox=after_box,static_title_white_pixels=title_white,held_white_mask_changed_pixels=changed))
 phase='Restoring pointer preference'
 call('input_cancel',hide=True);call('input_visibility',visible=initial['display_enabled'])
 phase='Completed - actual window and latest content'
 time.sleep(1);frame('final')
finally:
 stop.set();thread.join(10)
assert not errors,errors
result=dict(result='PASS_PHYSICAL_HDMI_PREVIEW',scope='Actual HDMI pixels. Frozen scene outside S-colored preview, representative move tile and optional whole-tile resize rim, final transformed window, pointer preference restored. Native intervals exclude UART; host/video command intervals include HTTP/UART. Two-second holds are intentional.',actions=actions,events=events,frames=frames,hardware=True)
(a.output/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
# Preserve measured capture timestamps; never accelerate playback.
duration=frames[-1]['time']-frames[0]['time'];times=[f['time']-frames[0]['time'] for f in frames]
avi=a.output/'preview.avi';video=cv2.VideoWriter(str(avi),cv2.VideoWriter_fourcc(*'MJPG'),10,(1280,820));assert video.isOpened()
for i in range(int(duration*10)+1):
 t=i/10;idx=max(0,bisect.bisect_right(times,t)-1);f=frames[idx];im=cv2.imread(str(a.output/'frames'/f['file']))
 canvas=np.full((820,1280,3),22,np.uint8);canvas[100:820]=im
 cv2.putText(canvas,f'Actual board HDMI | elapsed {t:.1f} s',(16,26),cv2.FONT_HERSHEY_SIMPLEX,.65,(240,240,240),1,cv2.LINE_AA)
 cv2.putText(canvas,f['phase'],(16,53),cv2.FONT_HERSHEY_SIMPLEX,.6,(130,220,255),1,cv2.LINE_AA)
 absolute=frames[0]['time']+t
 event=next((e for e in reversed(events) if e['start']<=absolute and e['op'] in ['input_button','input_move']),None)
 if event:
  active=absolute<event['end'];r=event['reply'];label=f'{event["op"]}: host {(min(absolute,event["end"])-event["start"])*1000:.0f} ms '+('in flight' if active else 'returned')
  if not active:
   if r.get('queued_to_uart'):label+=' | queued on PC; board completion polled separately'
   else:label+=f' | native accept {r.get("accept_ms",0):.0f} ms'+(f', complete {r["present_ms"]:.0f} ms' if 'present_ms' in r else ' (async completion polled)')
  cv2.putText(canvas,label,(16,82),cv2.FONT_HERSHEY_SIMPLEX,.5,(225,225,225),1,cv2.LINE_AA)
 video.write(canvas)
video.release()
target=a.output/'preview.mp4'
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(avi),'-c:v','libx264','-crf','23','-pix_fmt','yuv420p','-movflags','+faststart',str(target)],check=True)
print(json.dumps(dict(result=result['result'],actions=[{k:r[k] for k in ['action','frozen_bbox','preview_bbox','after_bbox','held_white_mask_changed_pixels']} for r in actions],video=str(target.resolve()),duration_seconds=duration)),flush=True)
