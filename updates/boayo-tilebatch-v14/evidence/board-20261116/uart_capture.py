import serial,time,pathlib,datetime,json
root=pathlib.Path(__file__).parent
p=serial.Serial(port=None,baudrate=115200,timeout=.05,rtscts=False,dsrdtr=False)
p.dtr=False;p.rts=False;p.port='COM4';p.open()
print('UART_CAPTURE_OPEN COM4 115200',flush=True)
with (root/'uart.log').open('ab',buffering=0) as log,(root/'uart_timeline.jsonl').open('a',buffering=1) as timeline:
 while not (root/'stop_uart').exists():
  data=p.read(max(1,p.in_waiting))
  if data:
   record=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),monotonic_ns=time.monotonic_ns(),offset=log.tell(),length=len(data))
   log.write(data);timeline.write(json.dumps(record)+'\n');print(data.decode('utf-8','replace'),end='',flush=True)
  for cmd in sorted(root.glob('send_*.txt')):
   payload=cmd.read_bytes();p.write(payload);p.flush();cmd.rename(cmd.with_suffix('.sent'))
p.close()