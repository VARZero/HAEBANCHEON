"""Compare receipts and final display separately for the native 16-point bursts."""
import argparse, json, math, re, statistics
from pathlib import Path

ap=argparse.ArgumentParser();ap.add_argument('log',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
def stats(values):
    v=sorted(values)
    return dict(n=len(v),median_ms=statistics.median(v),p95_ms=v[math.ceil(len(v)*.95)-1],maximum_ms=max(v))
report=dict(source=a.log.name,scope='Board-local native client timings; UART, capture and optical latency excluded. Three sequential 16-point bursts per condition. Async may omit intermediate positions.',conditions={})
text=a.log.read_text(errors='replace');coalesced=priority=0
for name in ['PAUSED','ACTIVE']:
    block=re.search(r'(?:^|\n)'+name+r'_BEGIN\r*\n(.*?)\n'+name+r'_END\r*\n',text,re.S)
    assert block and 'STREAM_BENCH_DONE' in block[1],name
    rounds=[json.loads(s) for s in re.findall(r'STREAM_SAMPLE (\{[^\r\n]+\})',block[1])]
    assert len(rounds)==3 and [r['round'] for r in rounds]==[0,1,2]
    receipts=[];tail=[];skipped=prioritized=0
    for r in rounds:
        assert r['requests']==16 and [s['index'] for s in r['samples']]==list(range(16))
        receipts.extend(s['accept_ms'] for s in r['samples'])
        if r['async']:
            p=r['progress'];assert p['visible_ticket']>=r['samples'][-1]['ticket'] and (p['visible_x'],p['visible_y'])==(700,350)
            skipped+=p['coalesced_moves']-coalesced;coalesced=p['coalesced_moves']
            prioritized+=p['priority_publications']-priority;priority=p['priority_publications']
            tail.append(r['final_visible_ms']-r['last_accept_ms']+r['samples'][-1]['accept_ms'])
        else:tail.append(r['samples'][-1]['present_ms'])
    metrics=dict(acceptance=stats(receipts),burst_final_visible=stats([r['final_visible_ms'] for r in rounds]),final_point_completion=stats(tail),coalesced_moves=skipped,priority_publications=prioritized)
    if not rounds[0]['async']:metrics['each_point_completion']=stats([s['present_ms'] for r in rounds for s in r['samples']])
    report['conditions'][name.lower()]=dict(rounds=rounds,metrics=metrics)
    print(name,json.dumps(metrics))
a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
