"""Timestamp-faithful playback of the median captured trial from each build."""
import argparse, bisect, json, statistics, subprocess
from pathlib import Path
import cv2
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
trials = []
for label in ['matrix-baseline','matrix-optimized']:
    folder = a.output/label
    data = json.loads((folder/'results.json').read_text(encoding='utf-8'))
    median = statistics.median(r['final_event_to_hdmi_ms'] for r in data['rounds'])
    r = min(data['rounds'], key=lambda r:abs(r['final_event_to_hdmi_ms']-median))
    start = r['events'][0]['time']
    frames = r['frames']
    trials.append(dict(report=r, times=[f['time']-start for f in frames],
                       images=[cv2.imread(str(folder/str(r['round'])/f['file'])) for f in frames],
                       final=r['events'][-1]['time']-start,
                       visible=r['events'][-1]['time']-start+r['final_event_to_hdmi_ms']/1000))
duration = max(t['visible'] for t in trials)+1.5
avi = a.output/'comparison.avi'
video = cv2.VideoWriter(str(avi),cv2.VideoWriter_fourcc(*'MJPG'),20,(1280,590))
assert video.isOpened()
def text(frame, s, x, y, color=(230,230,230), scale=.55):
    cv2.putText(frame,s,(x,y),cv2.FONT_HERSHEY_SIMPLEX,scale,color,1,cv2.LINE_AA)
for index in range(int(duration*20)+1):
    now = index/20
    canvas = np.full((590,1280,3),22,np.uint8)
    text(canvas,f'Actual board HDMI | 10 Hz latest-only mouse source | elapsed {now:.2f}s',18,26)
    text(canvas,'Median captured trial of three per build; recorded frame timestamps preserved.',18,49,scale=.5)
    for panel,t in enumerate(trials):
        x = panel*640
        status = 'FINAL ON HDMI' if now>=t['visible'] else 'FINAL INPUT SENT' if now>=t['final'] else 'SENDING INPUT'
        color = (130,220,120) if now>=t['visible'] else (150,180,250)
        name = 'BEFORE: old binary' if panel==0 else 'AFTER: optimized binary'
        text(canvas,name,x+15,78,color)
        text(canvas,f'Last input -> HDMI: {t["report"]["final_event_to_hdmi_ms"]:.0f} ms',x+15,101,color)
        frame = t['images'][max(0,bisect.bisect_right(t['times'],now)-1)]
        canvas[114:474,x:x+640] = cv2.resize(frame,(640,360))
        canvas[482:566,x:x+640] = cv2.resize(frame[650:710,160:620],(640,84))
        text(canvas,status,x+15,585,color,scale=.5)
    video.write(canvas)
    if index==84:
        cv2.imwrite(str(a.output/'comparison-preview.jpg'),canvas)
video.release()
target = a.output/'comparison.mp4'
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',str(avi),
                '-c:v','libx264','-crf','22','-pix_fmt','yuv420p','-movflags','+faststart',str(target)],check=True)
print(target.resolve())
