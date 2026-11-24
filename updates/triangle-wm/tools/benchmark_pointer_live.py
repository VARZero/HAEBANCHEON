"""Same 10 Hz mouse source + latest-only browser-style queue, actual HDMI."""
import argparse,json,time,threading,statistics
from pathlib import Path
from urllib.request import Request,urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('label');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
out=a.output/a.label;out.mkdir(parents=True,exist_ok=True)
url='http://127.0.0.1:8767'
def call(op,**args):
    start=time.perf_counter();req=Request(url+'/input',data=json.dumps(dict(op=op,args=args)).encode(),headers={'Origin':url,'Content-Type':'application/json'})
    with urlopen(req,timeout=90) as f:r=json.load(f)
    assert r.get('ok'),r
    return dict(start=start,end=time.perf_counter(),response=r)
with urlopen(url+'/status',timeout=10) as f:async_input=json.load(f).get('async_input',False)
def wait(ticket):
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        p=call('input_progress')
        if p['response']['result']['visible_ticket']>=ticket:return p
        time.sleep(.05)
    raise TimeoutError('not displayed')
rounds=[]
initial=call('input_status')['response']['result']['display_enabled']
try:
    call('input_cancel',hide=True);call('input_visibility',visible=True)
    for index in range(3):
        warm=call('input_move',x=200,y=680)
        if async_input:wait(warm['response']['result']['accepted_ticket'])
        folder=out/str(index);folder.mkdir(exist_ok=True)
        cv=threading.Condition();pending=None;producer_done=False;events=[];sent=[];errors=[];frames=[];stop=threading.Event();ready=threading.Event()
        def capture():
            try:
                while not stop.is_set():
                    t=time.perf_counter()
                    with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
                    observed=time.perf_counter();name=f'{len(frames):04d}.jpg';(folder/name).write_bytes(raw)
                    image=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
                    endpoint=int(np.count_nonzero(image[672:689,572:589].min(axis=2)>170))
                    frames.append(dict(time=observed,file=name,endpoint_white_pixels=endpoint));ready.set()
                    stop.wait(max(0,.1-(time.perf_counter()-t)))
            except Exception as e:errors.append(str(e));ready.set()
        def worker():
            global pending
            try:
                while True:
                    with cv:
                        cv.wait_for(lambda:pending is not None or producer_done)
                        if pending is None:return
                        point=pending;pending=None
                    result=call('input_move',x=point['x'],y=680);result['event']=point;sent.append(result)
            except Exception as e:errors.append(str(e))
        camera=threading.Thread(target=capture);camera.start();assert ready.wait(10)
        work=threading.Thread(target=worker);work.start();start=time.perf_counter()
        try:
            for i in range(20):
                time.sleep(max(0,start+i*.1-time.perf_counter()))
                event=dict(index=i,x=200+20*i,time=time.perf_counter());events.append(event)
                with cv:pending=event;cv.notify()
            with cv:producer_done=True;cv.notify()
            work.join(90);assert not work.is_alive() and not errors,errors
            assert sent[-1]['event']['index']==19
            completion=wait(sent[-1]['response']['result']['accepted_ticket']) if async_input else sent[-1]
            time.sleep(.8)
        finally:
            stop.set();camera.join(10)
        assert not errors,errors
        observed=next((f for f in frames if f['time']>=events[-1]['time'] and f['endpoint_white_pixels']>0),None)
        assert observed,'HDMI final cursor missing in endpoint region'
        report=dict(round=index,async_input=async_input,events=events,sent=sent,completion=completion,frames=frames,
                    transmitted=len(sent),host_coalesced=20-len(sent),
                    final_event_to_confirm_ms=(completion['end']-events[-1]['time'])*1000,
                    final_event_to_hdmi_ms=(observed['time']-events[-1]['time'])*1000,
                    acceptance_ms=[s['response']['accept_ms'] for s in sent])
        rounds.append(report);print(json.dumps({k:report[k] for k in ['round','async_input','transmitted','host_coalesced','final_event_to_confirm_ms','final_event_to_hdmi_ms']}),flush=True)
finally:
    call('input_cancel',hide=True);call('input_visibility',visible=initial)
    result=dict(label=a.label,scope='Three live-app trials. Twenty source positions at 10 Hz; one HTTP request in flight, newest queued coordinate only in BOTH modes. PC monotonic timestamps; HDMI includes USB capture/polling delay, not optical latency. Native acceptance durations exclude HTTP/UART overhead.',rounds=rounds)
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PACED_LIVE_DONE',a.label)
