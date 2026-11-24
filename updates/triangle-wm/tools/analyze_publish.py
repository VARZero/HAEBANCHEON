"""Extract complete board publication comparisons, retaining every raw sample."""
import argparse,json,statistics,math
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
text=a.log.read_text(errors='replace');assert 'PUBLISH_BENCH_DONE' in text and 'publish bench:' not in text
keys=['total_ms','explicit_copy_ms','present_remainder_ms','user_copy_ms','validate_ms','compare_upload_ms','configure_ms','select_ms','wait_ms','finish_ms','uploaded_words','command'];groups={}
for line in text.splitlines():
 if not line.startswith('PUB '):continue
 v=list(map(int,line.split()[1:]));assert len(v)==15
 mode,changes,index=v[:3];r=groups.setdefault((mode,changes),dict(mode=['checked_publish','full_upload_then_checked_present','trusted_copy'][mode],metadata_changes=bool(changes),samples=[]));row=dict(index=index)
 for i,key in enumerate(keys):row[key]=None if mode==1 and i>=3 else v[i+3]/(1e6 if i<3 else 1000 if i<10 else 1)
 r['samples'].append(row)
assert len(groups) in [4,6]
for r in groups.values():
 assert [x['index'] for x in r['samples']]==list(range(20));r['metrics']={}
 for key in keys[:-2]:
  if r['samples'][0][key] is None:continue
  v=sorted(x[key] for x in r['samples']);r['metrics'][key]=dict(median=statistics.median(v),p95=v[18],maximum=v[-1])
 print(r['mode'],r['metadata_changes'],{k:round(v['median'],3) for k,v in r['metrics'].items()})
a.output.write_text(json.dumps(dict(source=a.log.name,scope='Direct board-local synchronous calls, no WM/app. Two warmups and twenty samples per case. BO_PRESENT internal phases unavailable.',groups=list(groups.values())),indent=2)+'\n',encoding='utf-8')
