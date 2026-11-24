"""Validate and summarize the dedicated fixed WM/app/input deployment."""
import json, math, re, statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1]
folder = root/'evidence'/'factorial'/'final'
logs = [p for p in folder.glob('TRI_*.log') if '\nIPC_BENCH ' in p.read_text(errors='replace')]
log = max(logs,key=lambda p:p.stat().st_mtime)
text = log.read_text(errors='replace')
ipc = json.loads(re.search(r'(?:^|\n)IPC_BENCH (\{[^\r\n]+\})',text)[1])
rounds = [json.loads(s) for s in re.findall(r'(?:^|\n)STREAM_SAMPLE (\{[^\r\n]+\})',text)]
assert len(rounds)==3 and 'STREAM_BENCH_DONE' in text
states = []
for line in text.splitlines():
    if not line.startswith('{'):
        continue
    value = json.loads(line)
    if 'windows' in value and 'performance' in value:
        states.append(value)
state = states[-1]
assert len(state['windows'])==1, state
assert state['service']['numeric_format']=='fixed-q24', state['service']
assert state['service']['wire_version']=='BWM1', state['service']
assert '6.12.0-haebancheon-publishcopy' in text and 'tmpfs' in text
for r in rounds:
    p = r['progress']
    assert r['requests']==16 and p['visible_ticket']>=r['samples'][-1]['ticket']
    assert (p['visible_x'],p['visible_y'])==(700,350)
def stats(values):
    values=sorted(values)
    return dict(n=len(values),median_ms=statistics.median(values),p95_ms=values[math.ceil(.95*len(values))-1],maximum_ms=max(values))
report=dict(scope='Dedicated fixed WM + fixed diagnostic app/input clients, binary codec optimized; BO_PUBLISH_COPY retained. RAM/tmpfs, active RGB332 demo, same boot. Deployment validation, separate from the controlled common-client matrix.',
            source=log.name,build=json.loads((folder/'wm-build.json').read_text()),
            progress_roundtrip_with_active_app=stats(ipc['roundtrip_ms']),
            acceptance=stats([s['accept_ms'] for r in rounds for s in r['samples']]),
            final_point_completion=stats([r['final_visible_ms']-r['last_accept_ms']+r['samples'][-1]['accept_ms'] for r in rounds]),
            state=state,rounds=rounds)
(root/'results'/'factorial-final-fixed.json').write_text(json.dumps(report,indent=2)+'\n')
print('DEDICATED_FIXED_PASS',json.dumps({k:report[k] for k in ['progress_roundtrip_with_active_app','acceptance','final_point_completion']}))
