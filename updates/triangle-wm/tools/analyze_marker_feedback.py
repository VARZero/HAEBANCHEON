"""Observed triangle positions throughout capture; endpoint latency alone is insufficient."""
import argparse,json
from pathlib import Path
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('folder',type=Path);a=ap.parse_args();r=json.loads((a.folder/'result.json').read_text());out=[]
for t in r['rounds']:
 last=None;changes=[];start=t['events'][0]['time'];end=t['events'][-1]['time']
 for f in t['frames']:
  im=cv2.imread(str(a.folder/str(t['trial'])/f['file']));mask=((im[:,:,2]>150)&(im[:,:,1]<100)&(im[:,:,0]<100)).astype('uint8');_,_,stats,_=cv2.connectedComponentsWithStats(mask)
  candidates=[b for b in stats[1:] if b[3]>45 and b[4]>700];box=max(candidates,key=lambda b:b[4])[:4].tolist() if candidates else None
  if box!=last:changes.append(dict(seconds=f['time']-start,bbox=box));last=box
 out.append(dict(trial=t['trial'],changes_during_input=sum(0<=c['seconds']<=end-start for c in changes),changes=changes))
report=dict(scope='Distinct observed full S-triangle bounding boxes. Geometry quantizes motion; this is not a frame rate or individual-coordinate latency.',trials=out)
(a.folder/'position-changes.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
