"""Summarize first-frame trace. QEMU timings are never FPGA timings."""
from pathlib import Path
import json,re,sys
log,out=map(Path,sys.argv[1:3]);environment=sys.argv[3]
assert environment in ('hardware','qemu','host')
text=log.read_text(errors='replace');events={}
for event,wall,cpu,value in re.findall(r'BOAYO_FRAME_TRACE n=\d+ event=(\S+) wall_us=(\d+) cpu_us=(\d+) value=(-?\d+)',text):
 events.setdefault(event,dict(wall_us=int(wall),cpu_us=int(cpu),value=int(value)))
outer={}
for phase in ('output.versions','output.publish','output.upload'):
 if phase+'.begin' in events and phase+'.end' in events:
  a,b=events[phase+'.begin'],events[phase+'.end']
  outer[phase]={key:b[key]-a[key] for key in ('wall_us','cpu_us')}
fields=('user_copy_us','validate_us','upload_us','configure_us','select_us','wait_us','finish_us')
driver={name:events['publish.'+name]['value'] for name in fields if 'publish.'+name in events}
result=dict(environment=environment,hardware=environment=='hardware',outer=outer,driver_us=driver,
 driver_profile_complete=len(driver)==len(fields),
 note='Driver value fields are measured durations; outer intervals include trace output overhead. QEMU/host values do not predict FPGA speed.')
assert 'output.versions' in outer and 'output.publish' in outer
if environment=='hardware':assert result['driver_profile_complete'],'incomplete hardware driver trace'
out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
