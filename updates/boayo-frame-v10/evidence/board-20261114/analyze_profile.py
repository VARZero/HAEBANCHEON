from pathlib import Path
import json,sys
p=Path(sys.argv[1]);rows=[]
for line in p.read_text().splitlines():
 try:r=json.loads(line)
 except ValueError:continue
 if 'profile' in r and r.get('performance'):rows.append(r)
assert len(rows)>=2,(p,len(rows))
a,b=rows[0],rows[-1];n=b['performance']['render_count']-a['performance']['render_count'];assert n>0
result={'frames':n,'snapshot_interval_ms':(b['snapshot_time']-a['snapshot_time'])*1000/n,'per_frame':{},'errors':[b['render_error'],b['app_error']]}
for k,v in b['profile'].items():
 if not isinstance(v,list):continue
 d=[x-y for x,y in zip(v,a['profile'][k])]
 result['per_frame'][k]={'wall_ms':round(d[0]/1000/n,3),'cpu_ms':round(d[1]/1000/n,3),'calls':d[2]/n}
p.with_suffix('.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
