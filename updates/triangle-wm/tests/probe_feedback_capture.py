"""Explicit live-board test: input -> first observed HDMI change (not optical).

Run with the live app paused and exclusive input ownership. Polling and USB/UART
latency are included. Capture-card buffering is not calibrated out.
"""
import argparse, concurrent.futures, json, time
from pathlib import Path
from urllib.request import Request, urlopen
import cv2
import numpy as np

ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
url='http://127.0.0.1:8767'
def call(op,**kw):
    req=Request(url+'/input',data=json.dumps(dict(op=op,args=kw)).encode(),headers={'Origin':url,'Content-Type':'application/json'})
    with urlopen(req,timeout=60) as r:result=json.load(r)
    assert result['ok'],result
    return result
def frame():
    with urlopen('http://127.0.0.1:8766/frame',timeout=5) as r:raw=r.read()
    return raw,cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
def red(a):return (a[:,:,2]>140)&(a[:,:,1]<110)&(a[:,:,0]<120)
def white(a):return (a.min(axis=2)>170)
def control(action):return next(c for c in call('input_status')['result']['controls'] if c['action']==action)
samples=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
    def probe(name,op,kw,predicate):
        _,before=frame();start=time.perf_counter();future=pool.submit(call,op,**kw);detected=None;frames=0;best=None
        while not future.done() or time.perf_counter()-start<.3:
            raw,current=frame();frames+=1
            if detected is None and predicate(before,current):
                detected=(time.perf_counter()-start)*1000;best=raw
            time.sleep(.02)
        result=future.result();round_trip=(time.perf_counter()-start)*1000;time.sleep(.2);raw,current=frame()
        if detected is None and predicate(before,current):detected=(time.perf_counter()-start)*1000;best=raw
        assert detected is not None,(name,'no HDMI change')
        (args.output/(name+'-first.jpg')).write_bytes(best)
        (args.output/(name+'-final.jpg')).write_bytes(raw)
        row=dict(name=name,host_to_observed_hdmi_ms=detected,host_round_trip_ms=round_trip,frames=frames,**result)
        samples.append(row);print(json.dumps(row),flush=True)
    call('input_visibility',visible=False);call('input_cancel',hide=True)
    for action,dx,dy in [('move',70,-20),('resize',38,25)]:
        c=control(action);call('input_button',x=c['x'],y=c['y'],pressed=True)
        assert call('input_status')['result']['preview']['active']
        probe(action+'-preview','input_move',dict(x=c['x']+dx,y=c['y']+dy),lambda a,b:np.count_nonzero(red(a)^red(b))>150)
        probe(action+'-release','input_button',dict(x=c['x']+dx,y=c['y']+dy,pressed=False),lambda a,b:np.count_nonzero(white(b))<np.count_nonzero(white(a))*.4)
        assert not call('input_status')['result']['preview']['active']
    c=control('move');call('input_cancel',hide=True);_,clean=frame()
    call('input_button',x=c['x'],y=c['y'],pressed=True);call('input_move',x=c['x']-60,y=c['y'])
    call('input_cancel',hide=True);raw,restored=frame();time.sleep(.3);raw,restored=frame()
    assert np.count_nonzero(red(clean)^red(restored))<100,'cancel left outline'
    (args.output/'cancel-restored.jpg').write_bytes(raw)
    call('input_visibility',visible=True)
report=dict(passed=True,scope='PC HTTP input to first observed USB HDMI frame; includes UART, capture buffering and ~20-70ms polling. Not display optical latency.',samples=samples)
(args.output/'capture.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
