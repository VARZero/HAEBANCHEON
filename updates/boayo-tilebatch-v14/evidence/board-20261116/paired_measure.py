from board_io import command,status,root
import json,time

def stop():
 command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)

def measure(label,exe,profile=0,app='telemetry'):
 command(f'BOAYO_PROFILE={profile} BOAYO_INIT_TRACE=0 {exe} start',120)
 command(f'sleep 12; {exe} app {app}; sleep 30',120)
 raw=command(f'{exe} profile > /tmp/measure-a; sleep 60; {exe} profile > /tmp/measure-b; cat /tmp/measure-a; cat /tmp/measure-b',180)
 (root/(label+'-samples.txt')).write_text(raw)
 rows=[]
 for line in raw.splitlines():
  try:d=json.loads(line)
  except ValueError:continue
  if 'profile' in d and d.get('performance'):rows.append(d)
 assert len(rows)==2
 a,b=rows
 assert all(x['render_error'] is None and x['app_error'] is None for x in rows)
 assert b['projection']['geometry_builds']==0 and b['projection']['mask_builds']==0
 assert b['projection']['plan_cache']['rejected']==b['projection']['plan_cache']['misses']==0
 n=b['performance']['render_count']-a['performance']['render_count'];assert n>0
 r=dict(result='PASS_PHYSICAL_UPDATES',label=label,executable=exe,app=app,profile=profile,frames=n,
        seconds_per_update=(b['snapshot_time']-a['snapshot_time'])/n,
        snapshot_span=b['snapshot_time']-a['snapshot_time'],
        cache_before=a['projection']['texture_pool'],cache_after=b['projection']['texture_pool'],software={})
 if profile:
  for k,v in b['profile'].items():
   if isinstance(v,list):r['software'][k]=dict(wall_ms=(v[0]-a['profile'][k][0])/1000/n,cpu_ms=(v[1]-a['profile'][k][1])/1000/n,calls=(v[2]-a['profile'][k][2])/n)
 (root/(label+'.json')).write_text(json.dumps(r,indent=2))
 s=status(label+'-state')
 assert not s['output']['mock_backend']
 assert s['output']['memory_dma']['errors']==s['output']['memory_dma']['timeouts']==s['output']['poisoned']==0
 print(json.dumps(r),flush=True)
 return r

if __name__=='__main__':
 result=dict(result='RUNNING',workloads={},same_bitstream=True,same_kernel=True,same_rectangular_body=True,
             baseline='v13 update/reuse code with v14 rectangular UI; verified compatible plans',physical_board=True)
 for app in ('telemetry','dashboard'):
  a=measure('reference-'+app,'/tmp/boayo-reference',app=app);stop()
  b=measure('v14-'+app,'/usr/bin/boayo-native',app=app)
  result['workloads'][app]=dict(baseline=a,candidate=b,percent_time_reduction=(1-b['seconds_per_update']/a['seconds_per_update'])*100)
  (root/'paired-result.json').write_text(json.dumps(result,indent=2))
  if app!='dashboard':stop()
 result['result']='PASS_PAIRED_TELEMETRY_AND_DASHBOARD'
 (root/'paired-result.json').write_text(json.dumps(result,indent=2))
 print(json.dumps({app:dict(before=r['baseline']['seconds_per_update'],after=r['candidate']['seconds_per_update'],percent_time_reduction=r['percent_time_reduction']) for app,r in result['workloads'].items()}),flush=True)
