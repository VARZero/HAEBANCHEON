"""Play actual HDMI frames on their recorded timeline, alongside input targets."""
import argparse,bisect,json,statistics,subprocess
from pathlib import Path
import cv2,numpy as np
ap=argparse.ArgumentParser();ap.add_argument('--before',type=Path,required=True);ap.add_argument('--after',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
trials=[]
for folder in [a.before,a.after]:
 data=json.loads((folder/'result.json').read_text());median=data['median_event_to_hdmi_ms'];r=min(data['rounds'],key=lambda r:abs(r['final_event_to_hdmi_ms']-median));start=r['events'][0]['time']
 trials.append(dict(report=r,start=start,times=[f['time']-start for f in r['frames']],images=[cv2.imread(str(folder/str(r['trial'])/f['file'])) for f in r['frames']],final=r['events'][-1]['time']-start,visible=r['events'][-1]['time']-start+r['final_event_to_hdmi_ms']/1000))
duration=max(t['visible'] for t in trials)+1;avi=a.output/'marker-live-comparison.avi';video=cv2.VideoWriter(str(avi),cv2.VideoWriter_fourcc(*'MJPG'),30,(1280,500));assert video.isOpened()
def text(im,s,x,y,color=(230,230,230),scale=.52):cv2.putText(im,s,(x,y),cv2.FONT_HERSHEY_SIMPLEX,scale,color,1,cv2.LINE_AA)
for index in range(int(duration*30)+1):
 now=index/30;canvas=np.full((500,1280,3),22,np.uint8);text(canvas,f'Actual board HDMI | continuous 10 Hz input | elapsed {now:.2f}s',16,24);text(canvas,'Median of three trials. Actual frame times preserved. Crosshair is host input target.',16,46,scale=.49)
 for panel,t in enumerate(trials):
  left=panel*640;r=t['report'];text(canvas,'BEFORE: per-coordinate request/reply' if not panel else 'AFTER: newest-only one-way coordinates',left+12,72)
  text(canvas,f'Final input -> HDMI: {r["final_event_to_hdmi_ms"]:.0f} ms',left+12,95)
  image=t['images'][max(0,bisect.bisect_right(t['times'],now)-1)].copy()
  ev=r['events'][max(0,bisect.bisect_right([e['time']-t['start'] for e in r['events']],now)-1)];press=r['press']['reply']['result'];first=r['events'][0]
  # Press coordinates are available from the path: initial x = handle_x - 289.
  hx=first['x']+289;hy=r['events'][-1]['y']+131
  target=(round(639.5+ev['x']-hx),round(359.5+ev['y']-hy));cv2.drawMarker(image,target,(255,255,255),cv2.MARKER_CROSS,18,1)
  canvas[108:468,left:left+640]=cv2.resize(image,(640,360))
  state='FINAL ON HDMI' if now>=t['visible'] else 'FINAL INPUT SENT' if now>=t['final'] else 'SENDING INPUT'
  text(canvas,state,left+12,491,(130,220,120) if now>=t['visible'] else (160,190,255))
 video.write(canvas)
 if index==60:cv2.imwrite(str(a.output/'marker-live-comparison.jpg'),canvas)
video.release();target=a.output/'marker-live-comparison.mp4';subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(avi),'-c:v','libx264','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(target)],check=True);print(target.resolve())
