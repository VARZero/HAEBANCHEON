"""Observe actual capture pixels, independently of native output acknowledgments."""
import argparse,json,time
from pathlib import Path
from urllib.request import urlopen
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--seconds',type=float,default=60);a=ap.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
start=time.monotonic();rows=[];best=-1
deadline=time.monotonic()+30
while True:
 try:
  with urlopen('http://127.0.0.1:8766/frame',timeout=3) as f:ready=f.read()
  if ready:break
 except OSError:pass
 if time.monotonic()>=deadline:raise TimeoutError('Capture server not ready')
 time.sleep(.25)
start=time.monotonic()
while time.monotonic()-start<a.seconds:
 with urlopen('http://127.0.0.1:8766/frame',timeout=5) as f:raw=f.read()
 frame=cv2.imdecode(np.frombuffer(raw,np.uint8),cv2.IMREAD_COLOR)
 peak=int(frame.max());colored=int(np.count_nonzero(frame.max(axis=2)>10))
 row=dict(host_monotonic=time.monotonic(),seconds=time.monotonic()-start,peak=peak,nonblack_pixels=colored);rows.append(row)
 if colored>best:
  best=colored;(a.output/'best.jpg').write_bytes(raw);print(json.dumps(row),flush=True)
 time.sleep(.25)
(a.output/'observations.json').write_text(json.dumps(dict(scope='Actual USB capture pixels; independent of output-core acknowledgment',maximum_nonblack_pixels=best,rows=rows),indent=2)+'\n')
print('CAPTURE_OBSERVATION_DONE',best,flush=True)
