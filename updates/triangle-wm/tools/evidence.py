"""Publish measured board results and preserve phase-specific capture snapshots."""
import argparse,json,re,shutil,time
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('mode',choices=['begin','finish','snapshot']);ap.add_argument('stage');ap.add_argument('--output',required=True);ap.add_argument('--label');a=ap.parse_args();out=Path(a.output)
try:state=json.loads((out/'status.json').read_text(encoding='utf-8'))
except (OSError,ValueError):state={'metrics':[]}
if a.mode=='begin':state.update(stage=a.label or a.stage+'단계 검증 중',record='stage'+a.stage)
elif a.mode=='finish':
 matches=[]
 for path in out.glob('TRI_*.log'):
  for line in path.read_text(errors='replace').splitlines():
   if 'TRIANGLE_STAGE_RESULT ' in line:
    r=json.loads(line.split('TRIANGLE_STAGE_RESULT ',1)[1])
    if str(r['stage'])==a.stage:matches.append((path.stat().st_mtime_ns,r))
 if not matches:raise RuntimeError('No completed board report for this stage')
 report=max(matches,key=lambda v:v[0])[1];(out/('stage'+a.stage+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 for row in report['results']:
  state['metrics']=[m for m in state['metrics'] if m['name']!=row['name']]
  state['metrics'].append({k:round(v,3) if isinstance(v,float) else v for k,v in row.items() if k not in ('samples','preparation_ms')})
 state.update(stage=a.label or a.stage+'단계 실측 완료',record=None)
 print(json.dumps(state,ensure_ascii=False))
if a.mode in ('finish','snapshot'):shutil.copyfile(out/'latest.jpg',out/('stage'+a.stage+'-'+str(time.time_ns())+'.jpg'))
temp=out/'status.tmp';temp.write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8');temp.replace(out/'status.json')
