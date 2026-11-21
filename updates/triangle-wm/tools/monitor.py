"""Local HDMI capture preview; only the explicitly named capture card is opened."""
import argparse, json, threading, time
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import cv2
from pygrabber.dshow_graph import FilterGraph

ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--port',type=int,default=8766)
args=ap.parse_args();out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
devices=FilterGraph().get_input_devices(); index=devices.index('USB3.0 Video')
cap=cv2.VideoCapture(index,cv2.CAP_DSHOW)
cap.set(cv2.CAP_PROP_FOURCC,cv2.VideoWriter_fourcc(*'MJPG'))
cap.set(cv2.CAP_PROP_FRAME_WIDTH,1280);cap.set(cv2.CAP_PROP_FRAME_HEIGHT,720);cap.set(cv2.CAP_PROP_FPS,60)
if not cap.isOpened():raise RuntimeError('Capture card unavailable')
latest=b'';sequence=0;captured=0.;running=True
def capture():
 global latest,sequence,captured
 last_save=0;record=None;record_name=None;last_state=0;state={}
 with (out/'capture-timeline.jsonl').open('a',buffering=1) as log:
  while running:
   ok,frame=cap.read()
   if not ok:time.sleep(.1);continue
   captured=time.monotonic();sequence+=1
   if captured-last_state>1:
    try:state=json.loads((out/'status.json').read_text(encoding='utf-8'))
    except (OSError,ValueError):state={}
    last_state=captured
   name=state.get('record')
   if name!=record_name:
    if record:record.release()
    record=None;record_name=name
    if name and Path(name).name==name:
     record=cv2.VideoWriter(str(out/(name+'.avi')),cv2.VideoWriter_fourcc(*'MJPG'),30, (frame.shape[1],frame.shape[0]))
   if record:record.write(frame)
   ok,jpg=cv2.imencode('.jpg',frame,[cv2.IMWRITE_JPEG_QUALITY,82])
   if ok:latest=jpg.tobytes()
   log.write(json.dumps({'sequence':sequence,'host_monotonic':captured,'mean_bgr':frame.mean(axis=(0,1)).tolist(),'record':record_name})+'\n')
   if captured-last_save>1:
    # A Windows viewer/copy may briefly hold this file without write sharing.
    # Keep the live stream alive and retry the disk snapshot next second.
    try:(out/'latest.jpg').write_bytes(latest)
    except PermissionError:pass
    last_save=captured

class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def do_GET(self):
  if self.path.startswith('/stream'):
   self.send_response(200);self.send_header('Content-Type','multipart/x-mixed-replace; boundary=frame');self.send_header('Cache-Control','no-store');self.end_headers();prev=-1
   try:
    while running:
     if latest and sequence!=prev:
      data=latest;prev=sequence;self.wfile.write(b'--frame\r\nContent-Type: image/jpeg\r\nContent-Length: '+str(len(data)).encode()+b'\r\n\r\n'+data+b'\r\n')
     time.sleep(.05)
   except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
   return
  if self.path.startswith('/status'):
   try:data=json.loads((out/'status.json').read_text(encoding='utf-8'))
   except (OSError,ValueError):data={'stage':'연결 확인','metrics':[]}
   data.update(capture_sequence=sequence,capture_age_seconds=round(time.monotonic()-captured,2))
   body=json.dumps(data,ensure_ascii=False).encode();kind='application/json; charset=utf-8'
  elif self.path.startswith('/frame'):
   body=latest;kind='image/jpeg'
  else:
   body='''<!doctype html><html lang="ko"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>해반천 실보드 모니터</title><style>body{margin:20px;font:15px system-ui;background:#15191e;color:#eee}h1{font-size:20px}img{width:100%;max-width:1280px;background:#000}table{border-collapse:collapse}td,th{padding:10px;border-bottom:1px solid #444;text-align:left}#state{margin:12px 0}</style><h1>해반천 · 실제 HDMI 출력</h1><div id="state">캡처 연결 중</div><img src="/stream" alt="실제 보드 HDMI 캡처"><h2>단계별 지연시간</h2><table><thead><tr><th>단계 / 동작</th><th>중앙값 ms</th><th>P95 ms</th><th>최대 ms</th><th>범위</th></tr></thead><tbody id="metrics"></tbody></table><p>캡처 영상과 장치 완료 지연은 별도 측정입니다. 모니터 광학 지연은 포함하지 않습니다.</p><script>async function tick(){try{const s=await(await fetch('/status',{cache:'no-store'})).json();document.getElementById('state').textContent=s.stage+' · 캡처 '+s.capture_sequence+' · 최신 프레임 '+s.capture_age_seconds+'초 전';const t=document.getElementById('metrics');t.replaceChildren();for(const m of s.metrics||[]){const r=t.insertRow();for(const v of [m.name,m.median_ms,m.p95_ms,m.max_ms,m.scope])r.insertCell().textContent=v;}}catch(e){}setTimeout(tick,1000)}tick()</script></html>'''.encode();kind='text/html; charset=utf-8'
  self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

threading.Thread(target=capture,daemon=True).start()
print(json.dumps({'url':f'http://127.0.0.1:{args.port}','device':devices[index],'width':cap.get(3),'height':cap.get(4),'fps':cap.get(5)}),flush=True)
try:ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
finally:running=False;cap.release()
