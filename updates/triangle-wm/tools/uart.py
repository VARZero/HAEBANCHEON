"""One command at a time over the board console, preserving a raw transcript."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
ap=argparse.ArgumentParser();ap.add_argument('command');ap.add_argument('--timeout',type=float,default=60);ap.add_argument('--output',required=True)
a=ap.parse_args();out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
p=serial.Serial(port=None,baudrate=115200,timeout=.05);p.dtr=False;p.rts=False;p.port='COM4';p.open()
token='TRI_'+uuid.uuid4().hex[:10];start=time.monotonic();raw=b''
p.write((a.command+'; echo '+token+'_RC:$?\n').encode());p.flush()
pattern=token.encode()+rb'_RC:(\d+)\r*\n'
try:
 while time.monotonic()-start<a.timeout:
  chunk=p.read(max(1,p.in_waiting));raw+=chunk
  if chunk:print(chunk.decode(errors='replace'),end='',flush=True)
  m=re.search(pattern,raw)
  if m:break
 else:raise TimeoutError('UART command incomplete')
finally:
 p.close();(out/(token+'.log')).write_bytes(raw)
report={'command':a.command,'host_seconds':time.monotonic()-start,'rc':int(m[1])};(out/(token+'.json')).write_text(json.dumps(report,indent=2))
raise SystemExit(report['rc'])
