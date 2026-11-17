"""Boot exact SD payloads on QEMU RV32IMA. BOSIO is an explicitly labeled mock."""
from pathlib import Path
import sys,subprocess,tempfile,struct,zlib,selectors,time,os,re,json,hashlib
image=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
boot_only='--boot-only' in sys.argv
data=image.read_bytes();header=2048*512;assert data[header:header+8]==b'ORYSD001'
report=dict(result='RUNNING',image=str(image),image_sha256=hashlib.sha256(data).hexdigest(),hardware=False,rtl=False,mock_backend=True,checks=[])
with tempfile.TemporaryDirectory(prefix='boayo-native-qemu-') as temp,(out/'uart.log').open('wb') as log:
 root=Path(temp);cmd=['qemu-system-riscv32','-machine','virt','-cpu','rv32,c=false,f=false,d=false,v=false,zfa=false,zawrs=false','-m','1G','-smp','1','-bios','none','-display','none','-monitor','none','-serial','stdio','-no-reboot']
 pos=header+512
 for n,(name,address) in enumerate([('fw',0x80000000),('kernel',0x80400000),('dtb',0x87f00000)]):
  count,crc=struct.unpack_from('<II',data,header+8+n*8);payload=data[pos:pos+count*512];pos+=count*512;assert zlib.crc32(payload)==crc
  path=root/name;path.write_bytes(payload);cmd+=['-device',f'loader,file={path},addr={address},force-raw=on']
 (root/'entry').write_bytes(struct.pack('<4I',0x00000513,0x87f005b7,0x800002b7,0x00028067));cmd+=['-device',f'loader,file={root/"entry"},addr=0x80200000,force-raw=on,cpu-num=0']
 process=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,bufsize=0)
 sel=selectors.DefaultSelector();sel.register(process.stdout,selectors.EVENT_READ);buffer=bytearray()
 def until(pattern,timeout=600):
  end=time.monotonic()+timeout
  while True:
   hit=re.search(pattern,buffer)
   if hit:r=bytes(buffer[:hit.end()]);del buffer[:hit.end()];return r
   if not sel.select(max(0,end-time.monotonic())):raise TimeoutError(pattern)
   chunk=os.read(process.stdout.fileno(),16384)
   if not chunk:raise RuntimeError('QEMU exited')
   log.write(chunk);log.flush();buffer.extend(chunk)
   if b'Kernel panic' in buffer:raise RuntimeError('Kernel panic')
 def run(command,pattern=b'oryukdo# ',timeout=600):
  process.stdin.write((command+'; echo __NATIVE_RC:$?\r').encode());reply=until(rb'oryukdo# ',timeout)
  assert re.search(rb'\n__NATIVE_RC:0\r*\n',reply),(command,reply)
  assert re.search(pattern,reply),(command,reply)
  report['checks'].append(command);print('PASS_QEMU '+command,flush=True);return reply
 try:
  until(rb'oryukdo# ');report['checks'].append('BusyBox shell')
  run('cat /etc/oryukdo-image',rb'ORYUKDO-BoAYO-rawdma-v1')
  run('/bin/storage-check --identity', rb'IDENTITY bytes=')
  run('test -x /bin/bosio-dma-check')
  run('test -r /usr/share/boayo/assets/a0ebce13.bui')
  run('test ! -L /usr/bin/python && test ! -e /usr/bin/python3 && test ! -d /usr/lib/python3.12')
  run('echo $((19+23))',rb'\n42\r*\n')
  run('ddrcheck',rb'DDR_HIGH_OK pages=16')
  run('boayo-selftest',rb'BOAYO_NATIVE_SELFTEST_PASS')
  if not boot_only:
   run('boayo-dual-selftest',rb'BOAYO_DUAL_MODEL_PASS',1200)
   run('boayo-start --mock',rb'BoAYO C\+\+ service started',1200)
   def state():
    raw=run('boayo-status',rb'"runtime": "C\+\+17"',1200).decode(errors='replace');obj,_=json.JSONDecoder().raw_decode(raw[raw.index('{'):]);assert obj['render_error'] is None and obj.get('app_error') is None,obj;assert obj['output']['mock_backend'];return obj
   for _ in range(30):
    s=state()
    if s['output']['commits']>=2 and s['projection'].get('total_hw_jobs',0)>0:break
    run('sleep 1')
   else:raise AssertionError('Launcher never presented')
   assert any(w['title']=='BoAYO Launcher' for w in s['windows'])
   for kind,title in [('dashboard','Dashboard'),('telemetry','Telemetry'),('pulse','SDK Pulse')]:
    run('boayo-app '+kind,rb'BOAYO_APP_READY',1200);s=state();assert any(w['title']==title for w in s['windows'])
   process.stdin.write(b'boayo-keys\r');until(rb'BOAYO_KEYS_READY[^\r\n]*\r*\n',1200)
   process.stdin.write(b'dwas ');keys=until(rb'"left_release_serial":1',1200)
   assert b'"yaw":2.0' in keys and b'"pitch":2.0' in keys,keys
   process.stdin.write(b'\r\n');until(rb'"left_release_serial":2',1200)
   process.stdin.write(b'rq');tail=until(rb'oryukdo# ',1200);assert b'"left_release_serial":3' not in tail
   run('echo KEYS_SHELL_RESTORED',rb'\nKEYS_SHELL_RESTORED\r*\n')
   s=state();assert s['pointer']['left_release_serial']==2
   report['final_projection']=s['projection'];report['final_output']=s['output']
   run('boayo-stop')
   trace=(out/'uart.log').read_bytes()
   assert b'event=asset.loaded' in trace and b'event=desktop.end' in trace
   assert all(('event='+e).encode() not in trace for e in ('rounded.begin','circle.begin','rgb.dma.begin','surface.copy.begin'))
   report['checks'].append('prebuilt first launcher loaded without CPU rounded/circle drawing')
  report['result']='PASS_QEMU_ONLY'
 except BaseException as exc:
  report['result']='FAIL';report['error']=repr(exc);raise
 finally:
  process.terminate()
  try:process.wait(timeout=5)
  except subprocess.TimeoutExpired:process.kill();process.wait()
  (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
