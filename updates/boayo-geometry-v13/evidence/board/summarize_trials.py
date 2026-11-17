from pathlib import Path
import json,sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent
source=r/'comparison.json'
if not source.exists():source=r/'comparison-progress.json'
data=json.loads(source.read_text());trials=data.get('trials',data);out={}
for name,t in trials.items():
 p={x['step']:x['counters'] for x in t['profiles']};a,b=p[10],p[11]
 geometry={k:b['projection'][k]-a['projection'][k] for k in b['projection'] if k.startswith(('geometry_','mask_')) and type(b['projection'][k]) in (int,float)}
 phases={k:[b['profile'][k][i]-a['profile'][k][i] for i in range(3)] for k in b['profile'] if isinstance(b['profile'][k],list)}
 steps={x['step']:x for x in t['steps']}
 out[name]=dict(first_move_seconds=steps[11]['seconds'],geometry=geometry,phases_wall_us_cpu_us_calls=phases,steps=t['steps'])
if 'baseline' in out and 'optimized' in out:
 a,b=out['baseline'],out['optimized'];out['first_move_reduction_percent']=100*(1-b['first_move_seconds']/a['first_move_seconds'])
 out['geometry_descriptor_reduction_percent']=100*(1-b['geometry']['geometry_descriptor_us_total']/a['geometry']['geometry_descriptor_us_total'])
 out['note']='Paired v13 executable, v12 calculation mode versus optimized mode. Profiling enabled for both. Counters are nested, interval may include background refreshes; do not sum all phases. Physical v12 SD/kernel with new executable in RAM.'
(r/'timing-summary.json').write_bytes((json.dumps(out,indent=2)+'\n').encode())
print(json.dumps({k:v for k,v in out.items() if not isinstance(v,dict)},indent=2))
for name in ['baseline','optimized']:
 if name in out:print(name,out[name]['first_move_seconds'],out[name]['geometry'])
