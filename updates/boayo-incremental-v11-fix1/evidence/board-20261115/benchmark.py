from pathlib import Path
import json,re,time
from board_io import command,status
r=Path(__file__).parent
raw=command('cat /etc/oryukdo-image')
assert 'HAEBANCHEON-BoAYO-incremental-v11' in raw,'wrong SD image; stop before benchmarking'
def stop():command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
def measure(label,profile,legacy):
 command('BOAYO_PROFILE=%d BOAYO_DRIVER_LEGACY=%d BOAYO_INIT_TRACE=0 /usr/bin/boayo-native start'%(profile,legacy),180)
 command('sleep 12; /usr/bin/boayo-native app telemetry',180)
 command('sleep 20',60)
 raw=command('/usr/bin/boayo-native profile; sleep 40; /usr/bin/boayo-native profile',150)
 (r/(label+'.txt')).write_text(raw)
 rows=[]
 for line in raw.splitlines():
  try:d=json.loads(line)
  except ValueError:continue
  if 'profile' in d and d.get('performance'):rows.append(d)
 assert len(rows)==2 and all(x['app_error'] is None and x['render_error'] is None for x in rows)
 assert all(x['driver_profile']['available'] and x['driver_profile']['fast_paths']==(not legacy) for x in rows),rows
 a,b=rows;n=b['performance']['render_count']-a['performance']['render_count'];assert n>0
 report=dict(label=label,frames=n,seconds_per_update=(b['snapshot_time']-a['snapshot_time'])/n,profile=profile,legacy=legacy,software={},driver={})
 if profile:
  for name,v in b['profile'].items():
   if isinstance(v,list):report['software'][name]=dict(wall_ms=(v[0]-a['profile'][name][0])/1000/n,cpu_ms=(v[1]-a['profile'][name][1])/1000/n,calls=(v[2]-a['profile'][name][2])/n)
  for name,v in b['driver_profile'].items():
   if name.endswith('_us'):report['driver'][name]=(v-a['driver_profile'][name])/1000/n
   elif name not in ('available','enabled','fast_paths','version','units'):report['driver'][name]=(v-a['driver_profile'][name])/n
 (r/(label+'.json')).write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
 state=status(label+'-state');assert state['output']['memory_dma']['errors']==0 and state['output']['memory_dma']['timeouts']==0
 return report
reports=[]
reports.append(measure('legacy-driver-profile',1,1));stop()
reports.append(measure('fast-driver-profile',1,0));stop()
reports.append(measure('fast-driver-normal',0,0))
(r/'comparison.json').write_text(json.dumps(reports,indent=2))
print('BENCHMARK_COMPLETE_V11_RUNNING',flush=True)
