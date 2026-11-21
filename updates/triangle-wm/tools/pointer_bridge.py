"""Loopback-only mouse input bridge. Owns COM4 until /stop restores the console."""
import argparse,json,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.request import urlopen
import serial

ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);ap.add_argument('--port',type=int,default=8767);args=ap.parse_args()
out=Path(args.output);out.mkdir(parents=True,exist_ok=True);log=(out/'pointer-bridge.jsonl').open('a',encoding='utf-8',buffering=1)
uart=serial.Serial(port=None,baudrate=115200,timeout=.1);uart.dtr=False;uart.rts=False;uart.port='COM4';uart.open()
lock=threading.Lock();online=False;latest={};buffer=b''
def line(timeout=120):
 global buffer
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if b'\n' in buffer:
   result,buffer=buffer.split(b'\n',1);return result.decode(errors='replace').strip()
  buffer+=uart.read(max(1,uart.in_waiting))
 raise TimeoutError('Board input response timed out')
uart.write(b'stty -echo; /tmp/triangle-input-v8 --bridge; stty echo; echo INPUT_EXIT\n');uart.flush()
while True:
 text=line()
 if text=='INPUT_READY':online=True;break
def request(q):
 global latest,online
 with lock:
  if not online:raise RuntimeError('Input bridge is stopped')
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
  uart.write(b'quit\n');uart.flush()
  while line()!='INPUT_EXIT':pass
  online=False;uart.close()
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def send(self,obj,status=200):
  data=json.dumps(obj).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_GET(self):
  if self.path=='/status':
   try:
    with urlopen('http://127.0.0.1:8766/status',timeout=1) as response:capture=json.load(response)
   except Exception:capture={}
   return self.send({'online':online,'last':latest,'capture':capture})
  data=Path(__file__).with_name('pointer_monitor.html').read_bytes();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_POST(self):
  try:
   allowed={f'http://127.0.0.1:{args.port}',f'http://localhost:{args.port}'}
   if self.headers.get('Origin') not in allowed or self.headers.get('Content-Type')!='application/json':return self.send({'error':'Same-origin JSON required'},403)
   size=int(self.headers.get('Content-Length','0'))
   if not 0<size<=2048:return self.send({'error':'Invalid request size'},400)
   q=json.loads(self.rfile.read(size))
   if self.path=='/stop':stop();return self.send({'online':False})
   if self.path!='/input' or q.get('op') not in ('input_status','input_move','input_button','input_cancel'):return self.send({'error':'Invalid operation'},400)
   self.send(request(q))
  except Exception as e:self.send({'ok':False,'error':str(e)},500)
print(json.dumps({'url':f'http://127.0.0.1:{args.port}','input':'COM4','ready':True}),flush=True)
try:ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
finally:
 try:stop()
 finally:log.close()
