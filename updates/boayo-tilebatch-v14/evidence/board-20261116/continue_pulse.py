from paired_measure import measure,stop
from board_io import root
import json
rows=[]
for line in (root/'reference-dashboard-samples.txt').read_text().splitlines():
 try:d=json.loads(line)
 except ValueError:continue
 if 'profile' in d and d.get('performance'):rows.append(d)
assert len(rows)==2 and rows[0]['performance']['render_count']==rows[1]['performance']['render_count']
assert all(x['app_error'] is None and x['render_error'] is None for x in rows)
(root/'dashboard-idle.json').write_text(json.dumps(dict(result='PASS_IDLE_NO_REDUNDANT_RENDER',timing_comparison_applicable=False,
 explanation='Dashboard values remain unchanged without input/window count changes; use automatically changing SDK Pulse for the second workload.',snapshots=rows),indent=2))
result=json.loads((root/'paired-result.json').read_text())
stop()
a=measure('reference-pulse','/tmp/boayo-reference',app='pulse');stop()
b=measure('v14-pulse','/usr/bin/boayo-native',app='pulse')
result['workloads']['sdk-pulse']=dict(baseline=a,candidate=b,percent_time_reduction=(1-b['seconds_per_update']/a['seconds_per_update'])*100)
result['result']='PASS_PAIRED_TELEMETRY_AND_PULSE'
result['dashboard_note']='Static workload has no updates; recorded separately, excluded from speed comparison.'
result['performance_result']='REGRESSION' if any(v['percent_time_reduction']<0 for v in result['workloads'].values()) else 'IMPROVED'
(root/'paired-result.json').write_text(json.dumps(result,indent=2))
print(json.dumps({app:dict(before=r['baseline']['seconds_per_update'],after=r['candidate']['seconds_per_update'],percent_time_reduction=r['percent_time_reduction']) for app,r in result['workloads'].items()}),flush=True)
