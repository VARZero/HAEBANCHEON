"""Read one completed first-frame trace; do not poll the target or infer boot time."""
from pathlib import Path
import json,re,sys
log,out=map(Path,sys.argv[1:3]);environment=sys.argv[3]
assert environment in ('hardware','qemu','host')
events={}
for event,wall,cpu,value in re.findall(r'BOAYO_FRAME_TRACE n=\d+ event=(\S+) wall_us=(\d+) cpu_us=(\d+) value=(-?\d+)',log.read_text(errors='replace')):
 events.setdefault(event,dict(wall_us=int(wall),cpu_us=int(cpu),value=int(value)))
assert 'output.present.done' in events
phases={}
for name in ('plan.load','ready.prepare','tiles.prepare','texture.upload','projectors','output.versions','output.publish','output.upload'):
 a,b=events.get(name+'.begin'),events.get(name+'.end')
 if a and b:phases[name]={k:(b[k]-a[k])/1e6 for k in ('wall_us','cpu_us')}
report=dict(environment=environment,hardware=environment=='hardware',
 frame_to_present_seconds=(events['output.present.done']['wall_us']-events['frame.begin']['wall_us'])/1e6,
 daemon_to_present_seconds=None,phases_seconds=phases,
 note='Frame begins after daemon/desktop setup. This is not Linux boot time or complete daemon startup time. Output.upload includes versions and publish; do not sum nested spans. Host/QEMU time is not FPGA performance.')
out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
