"""Checked, acknowledged serial transfer through the repository's tiny receiver."""
import argparse,base64,gzip,hashlib,json,re,struct,time
from pathlib import Path
import serial
ap=argparse.ArgumentParser();ap.add_argument('source');ap.add_argument('target');ap.add_argument('--receiver',required=True);ap.add_argument('--output',required=True);ap.add_argument('--bootstrap',action='store_true');ap.add_argument('--verify',choices=['sha256','transport'],default='sha256',help='transport verifies receiver checksum, gzip CRC and extracted size; no board SHA256')
a=ap.parse_args();out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
require_target=re.fullmatch(r'/tmp/[A-Za-z0-9_.-]+',a.target)
if not require_target:raise ValueError('Target must be a simple /tmp path')
p=serial.Serial(port=None,baudrate=115200,timeout=.05);p.dtr=False;p.rts=False;p.port='COM4';p.open();log=(out/('transfer-'+str(time.time_ns())+'.log')).open('wb');buf=b''
def wait(pattern,timeout=30):
 global buf
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  m=re.search(pattern,buf)
  if m:result=buf[:m.end()];buf=buf[m.end():];return result
  data=p.read(max(1,p.in_waiting));log.write(data);log.flush();buf+=data
 raise TimeoutError((pattern,buf[-1000:]))
def write(data):p.write(data);p.flush()
try:
 if a.bootstrap:
  enc=base64.b64encode(Path(a.receiver).read_bytes())
  write(b"stty -echo; : > /tmp/triangle-rx.b64; echo BOOT_START\n");wait(rb'BOOT_START\r*\n')
  for off in range(0,len(enc),64):
   token=('PART_'+str(off)).encode();write(b"printf '"+enc[off:off+64]+b"' >> /tmp/triangle-rx.b64; echo "+token+b'\n');wait(token+rb'\r*\n')
  write(b"base64 -d /tmp/triangle-rx.b64 > /tmp/triangle-rx; chmod +x /tmp/triangle-rx; sha256sum /tmp/triangle-rx; echo BOOTSTRAP_READY\n")
  answer=wait(rb'BOOTSTRAP_READY\r*\n');expected=hashlib.sha256(Path(a.receiver).read_bytes()).hexdigest().encode();assert expected in answer,answer
 payload=Path(a.source).read_bytes();data=gzip.compress(payload,mtime=0);h=2166136261
 for b in data:h=((h^b)*16777619)&0xffffffff
 write(('/tmp/triangle-rx '+a.target+'.gz\n').encode());wait(rb'RX_READY\r*\n')
 write(struct.pack('<4I',0x31585242,len(data),h,(~len(data))&0xffffffff).hex().encode());rolling=2166136261;start=time.monotonic()
 for offset in range(0,len(data),256):
  part=data[offset:offset+256];write(part.hex().encode())
  for b in part:rolling=((rolling^b)*16777619)&0xffffffff
  ack=wait(rb'RX_ACK [0-9a-f]{8} [0-9a-f]{8}\r*\n')
  found=re.search(rb'RX_ACK ([0-9a-f]+) ([0-9a-f]+)',ack);assert int(found[1],16)==offset+len(part) and int(found[2],16)==rolling
  if offset%16384==0:print(json.dumps({'sent':offset+len(part),'total':len(data),'seconds':round(time.monotonic()-start,1)}),flush=True)
 wait(rb'RX_DONE\r*\n')
 wait(rb'haebancheon# ')
 # BusyBox wc scans the whole executable on this board. ls reads inode size;
 # transport FNV and gzip CRC already check the transferred bytes.
 verify=('sha256sum ' if a.verify=='sha256' else 'ls -ld ')+a.target
 write(('gzip -dc '+a.target+'.gz > '+a.target+' && chmod +x '+a.target+' && '+verify+' && echo TRANSFER_READY\n').encode())
 answer=wait(rb'(?:^|\n)TRANSFER_READY\r*\n',420);expected=hashlib.sha256(payload).hexdigest().encode()
 if a.verify=='sha256':assert expected in answer,answer
 else:
  plain=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',answer)
  size=re.search(rb'(?:^|\n)\S+\s+\d+\s+\S+\s+\S+\s+(\d+)\s+[^\r\n]*\s'+re.escape(a.target.encode())+rb'\r*\n',plain);assert size and int(size[1])==len(payload),answer
 print(json.dumps({'result':'PASS','target':a.target,'bytes':len(payload),'compressed':len(data),'sha256_host':expected.decode(),'verification':a.verify,'seconds':time.monotonic()-start}),flush=True)
finally:p.close();log.close()
