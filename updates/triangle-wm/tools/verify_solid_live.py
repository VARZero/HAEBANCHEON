"""Check the approved filled-resize interaction on actual USB HDMI capture."""
import argparse,json,time
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
base='http://127.0.0.1:8767';report={}
def call(op,**args):
 q=Request(base+'/input',data=json.dumps(dict(op=op,args=args)).encode(),headers={'Origin':base,'Content-Type':'application/json'})
 start=time.monotonic()
 with urlopen(q,timeout=60) as f:r=json.load(f)
 assert r.get('ok'),r
 r['host_roundtrip_ms']=(time.monotonic()-start)*1000
 return r
def shot(name,predicate):
 deadline=time.monotonic()+8
 while True:
  raw=urlopen('http://127.0.0.1:8766/frame',timeout=5).read();im=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
  if predicate(im):break
  assert time.monotonic()<deadline,('HDMI condition failed',name)
  time.sleep(.05)
 (a.output/(name+'.jpg')).write_bytes(raw);return im
def red(im):return int(((im[:,:,2]>150)&(im[:,:,1]<110)&(im[:,:,0]<100)).sum())
def green(im):return int(((im[:,:,1]>150)&(im[:,:,2]<110)&(im[:,:,0]<110)).sum())
def white(im):return int((im.min(axis=2)>170).sum())
def settle(r):
 seq=r['result']['queued_sequence'];deadline=time.monotonic()+20
 while True:
  p=call('input_progress')['result']
  if ((p['fast_visible_sequence']-seq)&0xffffffff)<0x80000000:return p
  assert time.monotonic()<deadline,p
  time.sleep(.05)
initial=call('input_status')['result'];assert not initial['pressed'];colors=initial['window_colors'][0];wid=colors['window_id'];pref=initial['display_enabled']
try:
 call('input_cancel',hide=True);call('input_visibility',visible=False)
 call('input_colors',window_id=wid,splash_index=228,background_index=84)
 original=shot('before',lambda im:white(im)>1000)
 h=next(c for c in call('input_status')['result']['controls'] if c['action']=='resize');x,y=h['x'],h['y']
 report['press']=call('input_button',x=x,y=y,pressed=True)
 filled=shot('filled',lambda im:red(im)>100000 and white(im)<100)
 p=call('input_status')['result']['preview'];assert p['mode']=='solid-resize' and p['payload_ops']==0 and p['payload_builds']==0
 report['initial_preview']=p
 settle(call('input_move',x=x-100,y=y-60));smaller=shot('shrink',lambda im:white(im)<100 and red(im)<red(filled)*.9)
 report['shrink_preview']=call('input_status')['result']['preview'];assert len(report['shrink_preview']['filled_tiles'])<len(p['filled_tiles'])
 settle(call('input_move',x=x+90,y=y+50));bigger=shot('grow',lambda im:white(im)<100 and red(im)>red(filled)*1.05)
 report['release']=call('input_button',x=x+90,y=y+50,pressed=False)
 shot('released',lambda im:white(im)>1000)
 final=call('input_status')['result'];assert not final['preview']['active'] and final['transform']['state']=='complete' and len(final['controls'])==3
 report['completed']=final
 # A non-default S uses the same filled payload path; B independently changes.
 report['color_change']=call('input_colors',window_id=wid,splash_index=3,background_index=93)
 changed=call('input_status')['result'];assert changed['window_colors'][0]==dict(window_id=wid,splash_index=3,background_index=93)
 h=next(c for c in changed['controls'] if c['action']=='resize')
 call('input_button',x=h['x'],y=h['y'],pressed=True);shot('green-splash',lambda im:green(im)>100000 and white(im)<100)
 call('input_cancel',hide=True);shot('cancelled',lambda im:white(im)>1000)
 report['checks']=['Captured content/control hidden; full footprint is solid S','Shrink removes former window pixels','Grow fills full footprint','Release shows B and content','Independent S/B color change','Cancel restores content']
 report['red_pixel_counts']=dict(initial=red(filled),shrink=red(smaller),grow=red(bigger))
finally:
 call('input_cancel',hide=True)
 call('input_colors',window_id=wid,splash_index=colors['splash_index'],background_index=colors['background_index'])
 call('input_visibility',visible=pref)
report.update(result='PASS_SOLID_LIVE',hardware=True,scope='Actual USB HDMI pixel checks. Host roundtrip includes UART and JSON status overhead, not a drag latency distribution.')
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(result=report['result'],red_pixel_counts=report['red_pixel_counts'],release_native_ms=report['release']['present_ms'])))
