from pathlib import Path
import time,struct,json,sys,hashlib
p=Path(__file__).parent;log=p/'uart.log'
def send(b):
 (p/('send_'+str(time.time_ns())+'.txt')).write_bytes(b)
def wait(token,start,timeout=40):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  with log.open('rb') as f:f.seek(start);data=f.read().replace(b'\r',b'')
  if token in data:return
  if b'RX_FAIL' in data:raise RuntimeError(data[-500:])
  time.sleep(.005)
 raise TimeoutError((token,data[-300:]))
def upload(source,target):
 b=Path(source).read_bytes();fnv=2166136261
 for x in b:fnv=((fnv^x)*16777619)&0xffffffff
 start=log.stat().st_size
 send(('/tmp/view-rx '+target+'; echo RX_EXIT:$?\r').encode());wait(b'RX_READY\n',start)
 send(struct.pack('<IIII',0x31585242,len(b),fnv,(~len(b))&0xffffffff).hex().encode())
 h=2166136261;begin=time.monotonic();last=begin
 for off in range(0,len(b),256):
  chunk=b[off:off+256]
  for x in chunk:h=((h^x)*16777619)&0xffffffff
  startchunk=log.stat().st_size;send(chunk.hex().encode())
  wait(('RX_ACK %08x %08x\n'%(off+len(chunk),h)).encode(),startchunk)
  now=time.monotonic()
  if now-last>=20:print(f'{target}: {off+len(chunk)}/{len(b)} bytes; {now-begin:.1f}s',flush=True);last=now
 wait(b'RX_DONE',startchunk);wait(b'RX_EXIT:0',startchunk)
 result=dict(source=str(source),target=target,bytes=len(b),fnv1a=h,sha256=hashlib.sha256(b).hexdigest(),seconds=round(time.monotonic()-begin,2),result='PASS_TRANSFER_FNV')
 (p/(Path(target).name+'-transfer.json')).write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':upload(sys.argv[1],sys.argv[2])
