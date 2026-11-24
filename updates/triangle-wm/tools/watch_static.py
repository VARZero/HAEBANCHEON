"""Observe unchanged title/control pixels during a board I/O stress trial."""
import argparse,json,time
from pathlib import Path
from urllib.request import urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('label');ap.add_argument('--seconds',type=int,default=90);a=ap.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'evidence'/'factorial'/'final'/a.label;out.mkdir(exist_ok=True)
rows=[];begin=time.monotonic();baseline=None
while time.monotonic()-begin<a.seconds:
    with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
    frame=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
    rects=[(410,235,820,350),(577,141,610,174),(673,545,706,578),(900,528,933,561)]
    white=[int(np.count_nonzero(frame[y0:y1,x0:x1].min(axis=2)>170)) for x0,y0,x1,y1 in rects]
    if baseline is None:baseline=white;assert min(baseline)>5,baseline
    ok=all(v>=b*.65 for v,b in zip(white,baseline));row=dict(seconds=time.monotonic()-begin,white=white,ok=ok);rows.append(row)
    if len(rows)%10==1 or not ok:
        (out/f'{len(rows):04}.jpg').write_bytes(raw);print(json.dumps(row),flush=True)
    time.sleep(1)
result=dict(label=a.label,baseline=baseline,samples=rows,all_static_visible=all(r['ok'] for r in rows))
(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print('STATIC_WATCH_DONE',a.label,result['all_static_visible'],flush=True)
