"""Extract real capture frames and quantify motion; not a photon-latency test."""
import cv2,json,sys
import numpy as np
from pathlib import Path
path=Path(sys.argv[1]);cap=cv2.VideoCapture(str(path));total=int(cap.get(cv2.CAP_PROP_FRAME_COUNT));results=[]
if total<=0:raise RuntimeError('Recording is still open or has no finalized frames')
for index in np.linspace(max(0,total-3600),max(0,total-1),61).astype(int):
 cap.set(cv2.CAP_PROP_POS_FRAMES,int(index));ok,frame=cap.read()
 if not ok:continue
 gray=frame.max(axis=2);mask=gray>35;y,x=np.where(mask)
 if len(x)<100:continue
 results.append({'frame':int(index),'area':int(len(x)),'cx':float(x.mean()),'cy':float(y.mean()),'bounds':[int(x.min()),int(y.min()),int(x.max()),int(y.max())]})
if not results:raise RuntimeError('No visible board output in sampled video')
chosen=[min(results,key=lambda r:r['cx']),max(results,key=lambda r:r['cx']),results[-1]]
for name,item in zip(['left','right','final'],chosen):
 cap.set(cv2.CAP_PROP_POS_FRAMES,item['frame']);ok,frame=cap.read()
 if ok:cv2.imwrite(str(path.with_name(path.stem+'-'+name+'.jpg')),frame)
report={'video':path.name,'frames':total,'sampled_nonblack':len(results),'centroid_x_span':max(r['cx'] for r in results)-min(r['cx'] for r in results),'centroid_y_span':max(r['cy'] for r in results)-min(r['cy'] for r in results),'samples':results}
path.with_suffix('.capture.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items() if k!='samples'}));cap.release()
