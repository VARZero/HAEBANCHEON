"""Loopback-only mouse input bridge. Owns COM4 until /stop restores the console."""
import argparse,json,threading,time,re
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.request import urlopen
import serial

ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--port',type=int,default=8767);ap.add_argument('--async-input',action='store_true');ap.add_argument('--fast-input',action='store_true');ap.add_argument('--motion-hz',type=float,default=30);ap.add_argument('--client',default='/tmp/triangle-input-v8');args=ap.parse_args()
if not (re.fullmatch(r'/tmp/[A-Za-z0-9_.-]+',args.client) or args.client=='/run/triangle-wm-runtime/triangle-input'):raise ValueError('Invalid input-client path')
if not 1<=args.motion_hz<=60:raise ValueError('Motion rate must be 1..60 Hz')
out=Path(args.output);out.mkdir(parents=True,exist_ok=True);log=(out/'pointer-bridge.jsonl').open('a',encoding='utf-8',buffering=1)
uart=serial.Serial(port=None,baudrate=115200,timeout=.1);uart.dtr=False;uart.rts=False;uart.port='COM4';uart.open()
lock=threading.Lock();online=False;latest={};buffer=b''
motion_cv=threading.Condition();pending_motion=None;motion_sequence=0;motion_sent=0
def line(timeout=120):
 global buffer
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if b'\n' in buffer:
   result,buffer=buffer.split(b'\n',1);return result.decode(errors='replace').strip()
  buffer+=uart.read(max(1,uart.in_waiting))
 raise TimeoutError('Board input response timed out')
mode='--bridge-fast' if args.fast_input else '--bridge-async' if args.async_input else '--bridge'
launch=('nice -n -5 ' if args.fast_input else '')+args.client
uart.write(('stty -echo; '+launch+' '+mode+'; stty echo; echo INPUT_EXIT\n').encode());uart.flush()
while True:
 text=line()
 if text=='INPUT_READY':online=True;break
 if text=='INPUT_EXIT':raise RuntimeError('Native input client exited before ready')

def flush_motion():
 global pending_motion,motion_sent
 with motion_cv:point=pending_motion;pending_motion=None
 if point:
  seq,x,y=point;uart.write(f'@{seq} {x} {y}\n'.encode());uart.flush();motion_sent=seq
def motion_worker():
 while online:
  with motion_cv:motion_cv.wait_for(lambda:pending_motion is not None or not online)
  if not online:return
  start=time.monotonic()
  with lock:
   if online:flush_motion()
  time.sleep(max(0,1/args.motion_hz-(time.monotonic()-start)))
if args.fast_input:threading.Thread(target=motion_worker,daemon=True).start()
def request(q):
 global latest,online,pending_motion,motion_sequence
 if args.fast_input and q['op']=='input_move':
  start=time.monotonic();a=q.get('args',{});x,y=a.get('x'),a.get('y')
  if type(x) is not int or type(y) is not int or not (0<=x<1280 and 0<=y<720):raise ValueError('Invalid pointer coordinates')
  with motion_cv:
   if not online:raise RuntimeError('Input bridge is stopped')
   motion_sequence=(motion_sequence+1)&0xffffffff or 1;sequence=motion_sequence;pending_motion=(sequence,x,y);motion_cv.notify()
  latest={'ok':True,'queued_to_uart':True,'host_queue_ms':(time.monotonic()-start)*1000,'result':{'queued_sequence':sequence}}
  return latest
 with lock:
  if not online:raise RuntimeError('Input bridge is stopped')
  if args.fast_input:flush_motion() # Button/status calls are ordered barriers.
  uart.write((json.dumps(q,separators=(',',':'))+'\n').encode());uart.flush()
  while True:
   text=line()
   if text=='INPUT_EXIT':online=False;raise RuntimeError('Board input process exited')
   if text.startswith('INPUT_REPLY '):
    latest=json.loads(text[12:]);log.write(json.dumps(latest)+'\n');return latest
def stop():
 global online
 with lock:
  if not online:return
  if args.fast_input:flush_motion()
  uart.write(b'quit\n');uart.flush()
  while line()!='INPUT_EXIT':pass
  online=False
  with motion_cv:motion_cv.notify_all()
  uart.close()
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def send(self,obj,status=200):
  data=json.dumps(obj).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_GET(self):
  if self.path=='/status':
   try:
    with urlopen('http://127.0.0.1:8766/status',timeout=1) as response:capture=json.load(response)
   except Exception:capture={}
   return self.send({'online':online,'async_input':args.async_input or args.fast_input,'fast_input':args.fast_input,'last':latest,'capture':capture,'queued_sequence':motion_sequence,'sent_sequence':motion_sent})
  data=Path(__file__).with_name('pointer_monitor.html').read_bytes();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_POST(self):
  try:
   allowed={f'http://127.0.0.1:{args.port}',f'http://localhost:{args.port}'}
   if self.headers.get('Origin') not in allowed or self.headers.get('Content-Type')!='application/json':return self.send({'error':'Same-origin JSON required'},403)
   size=int(self.headers.get('Content-Length','0'))
   if not 0<size<=2048:return self.send({'error':'Invalid request size'},400)
   q=json.loads(self.rfile.read(size))
   if self.path=='/stop':stop();return self.send({'online':False})
   if self.path!='/input' or q.get('op') not in ('input_status','input_progress','input_move','input_button','input_cancel','input_visibility','input_colors'):return self.send({'error':'Invalid operation'},400)
   self.send(request(q))
  except Exception as e:self.send({'ok':False,'error':str(e)},500)
print(json.dumps({'url':f'http://127.0.0.1:{args.port}','input':'COM4','ready':True}),flush=True)
try:ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
finally:
 try:stop()
 finally:log.close()
