"""Leave the dedicated fixed build controllable, and confirm its dot on HDMI."""
import json, time
from pathlib import Path
from urllib.request import Request, urlopen
import cv2
import numpy as np

root = Path(__file__).resolve().parents[1]
folder = root/'evidence'/'factorial'/'final'
base = 'http://127.0.0.1:8767'
deadline = time.monotonic()+40
while True:
    try:
        with urlopen(base+'/status',timeout=2) as f:
            status=json.load(f)
        if status['online']:
            break
    except Exception:
        pass
    assert time.monotonic()<deadline, 'Input bridge not ready'
    time.sleep(.2)
def call(op,**args):
    q=Request(base+'/input',data=json.dumps(dict(op=op,args=args)).encode(),
              headers={'Origin':base,'Content-Type':'application/json'})
    with urlopen(q,timeout=30) as f:
        r=json.load(f)
    assert r.get('ok'), r
    return r
call('input_visibility',visible=True)
move=call('input_move',x=700,y=680)
ticket=move['result']['accepted_ticket']
deadline=time.monotonic()+15
while True:
    progress=call('input_progress')['result']
    if progress['visible_ticket']>=ticket:
        break
    assert time.monotonic()<deadline, progress
    time.sleep(.1)
assert (progress['visible_x'],progress['visible_y'])==(700,680), progress
deadline=time.monotonic()+6
while True:
    with urlopen('http://127.0.0.1:8766/frame',timeout=3) as f:
        raw=f.read()
    frame=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
    white=int(np.count_nonzero(frame[672:689,692:709].min(axis=2)>170))
    if white:
        break
    assert time.monotonic()<deadline, 'Cursor absent on HDMI'
    time.sleep(.1)
(folder/'live-final.jpg').write_bytes(raw)
state=call('input_status')['result']
assert state['display_enabled'] and state['visible'] and not state['pressed']
assert not state['preview']['active'] and len(state['controls'])==3
rects=[(410,235,820,350),(577,141,610,174),(673,545,706,578),(900,528,933,561)]
static_white=[int(np.count_nonzero(frame[y0:y1,x0:x1].min(axis=2)>170)) for x0,y0,x1,y1 in rects]
assert static_white[0]>1000 and min(static_white[1:])>10, static_white
result=dict(scope='Dedicated fixed WM/app/input runtime handoff; one cursor endpoint confirmed on actual HDMI, not a latency distribution.',
            move=move,progress=progress,state=state,white_pixels=white,static_title_control_white_pixels=static_white,
            preview='http://127.0.0.1:8766',interactive=base)
(root/'results'/'factorial-final-live.json').write_text(json.dumps(result,indent=2)+'\n')
print('FINAL_LIVE_PASS',json.dumps(result))
