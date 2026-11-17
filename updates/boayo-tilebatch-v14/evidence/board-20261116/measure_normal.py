from board_io import command,status,root
import json,time
begin=time.monotonic()
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /usr/bin/boayo-native start',120)
command('sleep 12',30)
s=status('first-launcher')
assert s['performance']['render_count']>0
command('/usr/bin/boayo-native app telemetry; sleep 20',120)
status('telemetry-before')
raw=command('/usr/bin/boayo-native profile; sleep 45; /usr/bin/boayo-native profile',120)
(root/'v14-normal-samples.txt').write_text(raw)
rows=[]
for line in raw.splitlines():
 try:d=json.loads(line)
 except ValueError:continue
 if 'profile' in d and d.get('performance'):rows.append(d)
assert len(rows)==2,rows
a,b=rows
assert all(x['render_error'] is None and x['app_error'] is None for x in rows)
n=b['performance']['render_count']-a['performance']['render_count'];assert n>0
r=dict(result='PASS_PHYSICAL_TELEMETRY_UPDATES',frames=n,seconds_per_update=(b['snapshot_time']-a['snapshot_time'])/n,
       physical_board=True,profile=False,baseline_kind='historical v11/fix1, not same-session paired',
       historical_seconds_per_update=2.528884008888882,
       cache_before=a['projection']['texture_pool'],cache_after=b['projection']['texture_pool'])
r['change_percent_vs_historical']=(1-r['seconds_per_update']/r['historical_seconds_per_update'])*100
(root/'v14-normal.json').write_text(json.dumps(r,indent=2))
s=status('telemetry-after')
assert s['output']['memory_dma']['errors']==s['output']['memory_dma']['timeouts']==s['output']['poisoned']==0
print(json.dumps(r),flush=True)
