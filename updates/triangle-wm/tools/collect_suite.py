import json,math,sys
from pathlib import Path
out=Path(sys.argv[1]);stage=int(sys.argv[2]);plan=json.loads((out/('stage'+str(stage)+'.plan.json')).read_text(encoding='utf-8'))
logs=[]
for p in out.glob('TRI_*.log'):
 text=p.read_text(errors='replace')
 if '#stage '+str(stage)+'\n' in text and 'TRIANGLE_SUITE_DONE' in text:logs.append((p.stat().st_mtime_ns,p,text))
if not logs:raise RuntimeError('No completed suite')
_,path,text=max(logs);samples=[]
for line in text.splitlines():
 if line.startswith('TRIANGLE_SAMPLE '):samples.append(json.loads(line[len('TRIANGLE_SAMPLE '):]))
assert len(samples)==len(plan),(len(samples),len(plan));groups={};before=after=None
for expected,r in zip(plan,samples):
 assert r['index']==len([s for s in samples if s['index']<r['index']]);assert 'error' not in r['response'],r
 label=expected['label']
 if label=='before-pose':before=r['response']['commits']
 elif label=='after-pose':after=r['response']['commits']
 elif label=='pose':pose=r['response']
 elif label:groups.setdefault(label,[]).append(r)
assert before==after,'pose-only unexpectedly rebuilt scene'
results=[]
for name,rows in groups.items():
 values=sorted(r['ipc_ms'] for r in rows);median=(values[(len(values)-1)//2]+values[len(values)//2])/2;results.append({'name':name,'n':len(rows),'median_ms':median,'p95_ms':values[math.ceil(len(values)*.95)-1],'max_ms':values[-1],'scope':'C client send → WM response; output acknowledged','samples':rows})
results.append({'name':str(stage)+'단계 / 출력 시점','n':pose['n'],'median_ms':pose['median_ms'],'p95_ms':pose['p95_ms'],'max_ms':pose['max_ms'],'scope':pose['scope'],'preparation_ms':pose['preparation_ms'],'samples':pose['samples_ms']})
report={'stage':stage,'harness':'board_client.c','raw_log':path.name,'results':results,'state':samples[-1]['response']};(out/('stage'+str(stage)+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
state=json.loads((out/'status.json').read_text(encoding='utf-8'));names={r['name'] for r in results};state['metrics']=[r for r in state['metrics'] if r['name'] not in names]
state['metrics'] += [{k:round(v,3) if isinstance(v,float) else v for k,v in r.items() if k not in ('samples','preparation_ms')} for r in results]
state.update(stage=str(stage)+'단계 실측 완료 · HDMI 검토',record=None);tmp=out/'status.tmp';tmp.write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8');tmp.replace(out/'status.json')
for r in results:print(json.dumps({k:v for k,v in r.items() if k!='samples'},ensure_ascii=False))
