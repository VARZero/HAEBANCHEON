from pathlib import Path
import json
folder=Path('D:/temp/haebancheon-motion-v7/board-trial-20261114');out=[]
for p in sorted(folder.glob('BODMA_*.json')):
 d=json.loads(p.read_text());raw=d.get('uart','')
 if 'status' not in d.get('command','') or '"performance"' not in raw:continue
 try:
  state=json.JSONDecoder().raw_decode(raw[raw.index('{'):])[0]
  out.append(dict(file=p.name,time=d.get('observed_at'),command=d['command'],performance=state['performance'],projection=state.get('projection'),windows=[w.get('title') for w in state['windows'] if w.get('mapped')],view=state.get('view')))
 except (ValueError,KeyError):pass
out.sort(key=lambda x:x['time']);dest=Path(__file__).parent/'historical-baseline.json';dest.write_text(json.dumps(out,indent=2))
for d in out:print(d['time'],d['file'],d['windows'],d['performance'].get('last_compose_ms'),d['projection'].get('jobs_per_core'),d['command'])
