"""Actual USB HDMI + shared, logged UART console. Loopback access only."""
import argparse,base64,collections,gzip,hashlib,json,re,struct,subprocess,threading,time,uuid
from datetime import datetime
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import cv2,numpy as np,serial

ap=argparse.ArgumentParser();ap.add_argument('--port',type=int,default=8770);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
cv=threading.Condition();serial_write=threading.Lock();command_lock=threading.RLock();events=collections.deque(maxlen=2000);serial_tail=bytearray();latest=b'';frame_seq=0;frame_time=0.;nonblack=0;mode='shell';running=True;capture_process=None;interruptions=0
raw_log=(a.output/'uart-raw.log').open('ab',buffering=0);event_log=(a.output/'console.jsonl').open('a',encoding='utf-8',buffering=1);capture_log=(a.output/'capture.log').open('ab',buffering=0)
uart=serial.Serial(port=None,baudrate=115200,timeout=.1);uart.dtr=False;uart.rts=False;uart.port='COM4';uart.open()
def emit(kind,text):
 with cv:
  item={'id':events[-1]['id']+1 if events else 1,'time':datetime.now().strftime('%H:%M:%S.%f')[:-3],'kind':kind,'text':text};events.append(item);event_log.write(json.dumps(item,ensure_ascii=False)+'\n');cv.notify_all()
def send(data,label):
 emit('tx',label)
 with serial_write:uart.write(data);uart.flush()
def read_uart():
 global mode
 while running:
  try:
   data=uart.read(max(1,uart.in_waiting))
   if not data:continue
   raw_log.write(data);emit('rx',re.sub(r'\x1b\[[0-9;?]*[ -/]*[@-~]','',data.decode('utf-8',errors='replace')).replace('\r',''))
   with cv:
    serial_tail.extend(data)
    if len(serial_tail)>1048576:del serial_tail[:-524288]
    tail=bytes(serial_tail[-2048:]).replace(b'\r',b'')
    ready=tail.rfind(b'INPUT_OPEN stdin');prompt=tail.rfind(b'haebancheon# ')
    if mode=='starting':
     # A previous command's final shell prompt can arrive after /tty starts.
     # Only a prompt following this invocation's echo indicates startup failure.
     invocation=tail.rfind(b'boayo-input\n')
     if ready>=0:mode='tty'
     elif invocation>=0 and prompt>invocation:mode='shell'
    elif mode in ('tty','stopping') and prompt>=0 and prompt>ready:mode='shell'
    cv.notify_all()
  except Exception as e:emit('error','UART: '+str(e));break
def capture():
 global latest,frame_seq,frame_time,nonblack,capture_process
 while running:
  try:
   capture_process=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','warning','-f','dshow','-rtbufsize','128M','-video_size','1280x720','-framerate','30','-i','video=USB3.0 Video','-an','-c:v','mjpeg','-q:v','5','-flush_packets','1','-f','image2pipe','pipe:1'],stdout=subprocess.PIPE,stderr=capture_log,bufsize=0)
   buffer=bytearray();last_sample=0.
   while running:
    chunk=capture_process.stdout.read(65536)
    if not chunk:break
    buffer.extend(chunk)
    while True:
     start=buffer.find(b'\xff\xd8');end=buffer.find(b'\xff\xd9',max(0,start))
     if start<0 or end<0:break
     frame=bytes(buffer[start:end+2]);del buffer[:end+2];now=time.monotonic()
     if now-last_sample>.5:
      image=cv2.imdecode(np.frombuffer(frame,np.uint8),cv2.IMREAD_COLOR)
      if image is not None:nonblack=int(np.count_nonzero(image.max(axis=2)>12));(a.output/'latest.jpg').write_bytes(frame)
      last_sample=now
     with cv:latest=frame;frame_seq+=1;frame_time=now;cv.notify_all()
    if len(buffer)>8388608:buffer.clear()
   capture_process.wait(timeout=5)
  except Exception as e:emit('error','캡처: '+str(e))
  if running:time.sleep(1)
def status(after=0):
 with cv:return {'mode':mode,'last_event_id':events[-1]['id'] if events else 0,'uart':'COM4 / 115200 / 실제 보드','capture_sequence':frame_seq,'capture_age_ms':round((time.monotonic()-frame_time)*1000) if frame_time else None,'nonblack_pixels':nonblack,'events':[e for e in events if e['id']>after]}
def command(text,timeout):
 if not text or len(text.encode())>900 or '\n' in text or '\r' in text:raise ValueError('한 줄 명령, 최대 900바이트')
 with command_lock:
  if mode!='shell':raise ValueError('먼저 키 입력 앱을 종료해 콘솔로 돌아오세요')
  token='HB_'+uuid.uuid4().hex[:10];start=time.monotonic();stamp=interruptions
  with cv:serial_tail.clear()
  send((text+"; printf '\\n"+token+":%s\\n' $?\n").encode(),text)
  pattern=re.compile(token.encode()+rb':(\d+)\r*\n')
  with cv:
   while True:
    if interruptions!=stamp:raise InterruptedError('명령을 중지했습니다')
    match=pattern.search(serial_tail)
    if match:return {'rc':int(match[1]),'host_ms':round((time.monotonic()-start)*1000,2),'output':bytes(serial_tail).decode(errors='replace')}
    remain=timeout-(time.monotonic()-start)
    if remain<=0:raise TimeoutError('응답을 기다리는 중입니다. 실제 콘솔을 확인하세요.')
    cv.wait(min(remain,.5))
def upload(source,target):
 global mode
 source=Path(source).resolve();allowed=Path('D:/temp/haebancheon-keyboard-v16').resolve()
 if not source.is_relative_to(allowed) or not source.is_file() or source.stat().st_size>8388608:raise ValueError('승인된 개발본 폴더의 파일만 전송할 수 있습니다')
 if not re.fullmatch(r'/tmp/[A-Za-z0-9_.-]+',target):raise ValueError('단순한 /tmp 파일명만 허용')
 with command_lock:
  if mode!='shell':raise ValueError('먼저 키 입력 앱을 종료하세요')
  receiver=allowed/'uart-receive';receiver_bytes=receiver.read_bytes();receiver_sha=hashlib.sha256(receiver_bytes).hexdigest()
  payload=source.read_bytes();data=gzip.compress(payload,mtime=0);started=time.monotonic();stamp=interruptions
  result=command('stty -echo; if test -x /tmp/hb-uart-rx; then sha256sum /tmp/hb-uart-rx; fi',45)
  if receiver_sha not in result['output']:
   result=command(': >/tmp/hb-uart-rx.b64',30)
   encoded=base64.b64encode(receiver_bytes).decode()
   for offset in range(0,len(encoded),640):
    result=command("printf '"+encoded[offset:offset+640]+"' >> /tmp/hb-uart-rx.b64",45)
    if result['rc']:raise ValueError('수신기 준비 실패')
   result=command('base64 -d /tmp/hb-uart-rx.b64 >/tmp/hb-uart-rx && chmod +x /tmp/hb-uart-rx && sha256sum /tmp/hb-uart-rx',45)
   if result['rc'] or receiver_sha not in result['output']:raise ValueError('수신기 SHA-256 불일치')
  checksum=2166136261
  for byte in data:checksum=((checksum^byte)*16777619)&0xffffffff
  mode='upload'
  def wait(pattern,timeout=30):
   expression=re.compile(pattern);end=time.monotonic()+timeout
   with cv:
    while True:
     if interruptions!=stamp:raise InterruptedError('전송을 중지했습니다')
     match=expression.search(serial_tail)
     if match:answer=bytes(serial_tail[:match.end()]);del serial_tail[:match.end()];return answer
     if time.monotonic()>=end:raise TimeoutError('전송 응답 시간 초과')
     cv.wait(.1)
  # Reuse the repository's 1.9 KiB syscall-only receiver and its block ACKs.
  launch='nice -n -15 /tmp/hb-uart-rx '+target+'.gz.part'
  try:
   with cv:serial_tail.clear()
   send((launch+'\n').encode(),'검증된 소프트웨어 전송 시작: '+source.name)
   wait(rb'RX_READY\r*\n',45);rolling=2166136261
   with serial_write:uart.write(struct.pack('<4I',0x31585242,len(data),checksum,(~len(data))&0xffffffff).hex().encode());uart.flush()
   for offset in range(0,len(data),256):
    part=data[offset:offset+256]
    for byte in part:rolling=((rolling^byte)*16777619)&0xffffffff
    with serial_write:uart.write(part.hex().encode());uart.flush()
    answer=wait(rb'RX_ACK [0-9a-f]{8} [0-9a-f]{8}\r*\n',20);found=re.search(rb'RX_ACK ([0-9a-f]+) ([0-9a-f]+)',answer)
    if int(found[1],16)!=offset+len(part) or int(found[2],16)!=rolling:raise ValueError('전송 체크섬 불일치')
    if offset%49152==0:emit('info',f'전송 {offset+len(part):,} / {len(data):,} 바이트')
   wait(rb'RX_DONE\r*\n',20);wait(rb'haebancheon# ',15);mode='shell'
  except Exception:
   mode='stopping'
   with serial_write:uart.write(b'!'*512+b'\x03\n');uart.flush()
   raise
  emit('info','전송 체크섬 확인 완료. 압축 해제와 SHA-256 검사를 진행합니다.')
  verification=command('gzip -dc '+target+'.gz.part > '+target+' && chmod +x '+target+' && sha256sum '+target+'; result=$?; stty echo; (exit "$result")',120)
  expected=hashlib.sha256(payload).hexdigest()
  if verification['rc'] or expected not in verification['output']:raise ValueError('보드 SHA-256 확인 실패')
  emit('info','보드 SHA-256 일치: '+expected);return {'target':target,'sha256':expected,'bytes':len(payload),'compressed_bytes':len(data),'host_seconds':round(time.monotonic()-started,2)}
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def json(self,data,status=200):
  data=json.dumps(data,ensure_ascii=False).encode();self.send_response(status);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_GET(self):
  try:
   if self.path.startswith('/status'):
    after=int(self.path.split('after=')[-1]) if 'after=' in self.path else 0;return self.json(status(after))
   if self.path=='/frame':
    with cv:frame=latest
    self.send_response(200 if frame else 503);self.send_header('Content-Type','image/jpeg');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(frame);return
   if self.path.startswith('/stream'):
    self.send_response(200);self.send_header('Content-Type','multipart/x-mixed-replace; boundary=hdmi');self.send_header('Cache-Control','no-store');self.end_headers();seen=-1
    while running:
     with cv:
      cv.wait_for(lambda:frame_seq!=seen and bool(latest),timeout=2);seen=frame_seq;frame=latest
     if frame:self.wfile.write(b'--hdmi\r\nContent-Type: image/jpeg\r\nContent-Length: '+str(len(frame)).encode()+b'\r\n\r\n'+frame+b'\r\n');self.wfile.flush()
    return
   if self.path.split('?')[0]!='/':return self.json({'error':'not found'},404)
   data=Path(__file__).with_name('board_monitor.html').read_bytes();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  except (BrokenPipeError,ConnectionResetError):pass
 def do_POST(self):
  global mode,interruptions
  try:
   if self.headers.get('Origin') not in (f'http://127.0.0.1:{a.port}',f'http://localhost:{a.port}') or self.headers.get_content_type()!='application/json':return self.json({'error':'동일 출처 JSON 요청만 허용'},403)
   size=int(self.headers.get('Content-Length','0'))
   if not 0<size<=2048:return self.json({'error':'요청 크기 초과'},400)
   body=json.loads(self.rfile.read(size))
   if self.path=='/tty' and not body.get('enabled'):
    with cv:interruptions+=1;mode='stopping';cv.notify_all()
    send(b'\x03\n','^C · 중지 / 콘솔 복귀');return self.json({'mode':mode})
   if self.path=='/upload':return self.json(upload(body['source'],body['target']))
   if self.path=='/command':return self.json(command(body['command'],min(180,max(1,float(body.get('timeout',30))))))
   if self.path=='/tty':
    with command_lock:
     with cv:serial_tail.clear()
     if body.get('enabled'):
      if mode!='shell':raise ValueError('현재 콘솔 상태를 먼저 확인하세요')
      mode='starting';send(b'boayo-input\n','boayo-input · 현재 TTY의 키 전달 시작')
     else:mode='stopping';send(b'\x03','^C · 입력 앱 종료 / 콘솔 복귀')
    return self.json({'mode':mode})
   if self.path=='/key':
    if mode!='tty':raise ValueError('TTY 키 입력을 먼저 시작하세요')
    key=body['key'];keys={'Home':b'\x1b[H','ArrowUp':b'\x1b[A','ArrowDown':b'\x1b[B','ArrowRight':b'\x1b[C','ArrowLeft':b'\x1b[D','Enter':b'\r'}
    if key not in keys:raise ValueError('지원하지 않는 키')
    send(keys[key],key+' → 실제 UART');return self.json({'sent':True,'key':key,'host_monotonic':time.monotonic()})
   return self.json({'error':'not found'},404)
  except Exception as e:self.json({'error':str(e)},400)
emit('info','실제 UART와 USB HDMI 캡처를 연결했습니다. 명령과 응답을 기록합니다.')
threading.Thread(target=read_uart,daemon=True).start();threading.Thread(target=capture,daemon=True).start()
print('BOARD_MONITOR_READY http://127.0.0.1:'+str(a.port),flush=True)
try:ThreadingHTTPServer(('127.0.0.1',a.port),Handler).serve_forever()
finally:
 running=False
 if capture_process and capture_process.poll() is None:capture_process.terminate()
 uart.close();raw_log.close();event_log.close();capture_log.close()
