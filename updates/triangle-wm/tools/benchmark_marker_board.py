"""Native board marker comparison; UART transfer and HDMI capture kept separate."""
import argparse,json,math,re,statistics,subprocess,sys,time
from pathlib import Path
ap=argparse.ArgumentParser()
ap.add_argument('--output',type=Path,required=True)
ap.add_argument('--samples',type=int,default=16)
ap.add_argument('--modes',nargs='+',choices=['tile','outline'],default=['tile','outline'])
ap.add_argument('--existing-first',action='store_true')
ap.add_argument('--require-rim',action='store_true')
ap.add_argument('--require-full-move',action='store_true')
a=ap.parse_args()
assert 4<=a.samples<=256
root=Path(__file__).resolve().parents[1]
a.output.mkdir(parents=True,exist_ok=True)
def status(stage,metrics=None):
    (a.output/'status.json').write_text(json.dumps(dict(stage=stage,metrics=metrics or []),ensure_ascii=False),encoding='utf-8')
def command(text,label,timeout=600):
    out=a.output/label
    start=time.monotonic()
    with (a.output/(label+'.console.log')).open('w',encoding='utf-8') as log:
        subprocess.run([sys.executable,str(root/'tools/uart.py'),text,'--timeout',str(timeout),'--output',str(out)],stdout=log,stderr=subprocess.STDOUT,check=True)
    files=list(out.glob('TRI_*.log'))
    raw=max(files,key=lambda p:p.stat().st_mtime).read_text(errors='replace')
    (a.output/(label+'.host.json')).write_text(json.dumps(dict(host_start=start,host_end=time.monotonic(),command=text,scope='Host command including UART, not native latency'),indent=2)+'\n')
    return raw
def stats(values):
    v=sorted(values);return dict(n=len(v),median_ms=statistics.median(v),p95_ms=v[math.ceil(.95*len(v))-1],max_ms=max(v))
report=dict(hardware=True,scope='Native RV32 client on physical board; UART transfer/printing, USB capture and optical latency excluded. Preview completion is output publication acknowledgment.',samples_per_action=a.samples,modes={})
for index,mode in enumerate(a.modes):
    status(mode+' 프리뷰 측정 준비')
    if index or not a.existing_first:
        command('triangle-fixed-start stop; sleep 3; TRIANGLE_PREVIEW='+mode+' triangle-fixed-start start; echo MODE_'+mode,'start-'+mode,timeout=600)
    command('n=150; while ! grep -q initial_content /tmp/triangle-app.log; do kill -0 $(cat /run/triangle-app.pid) || exit 1; n=$((n-1)); test "$n" -gt 0 || exit 1; sleep 2; done; echo APP_READY','ready-'+mode,timeout=600)
    status(mode+' 이동·크기 조절 지연 측정 중')
    command('/run/triangle-wm-runtime/triangle-input --marker-bench '+str(a.samples)+' > /tmp/marker-'+mode+'.log 2>&1; r=$?; tail -c 180 /tmp/marker-'+mode+'.log; test "$r" = 0','run-'+mode,timeout=1200)
    raw=command('cat /tmp/marker-'+mode+'.log','read-'+mode,timeout=240)
    assert 'MARKER_BENCH_DONE' in raw
    rows=[json.loads(x) for x in re.findall(r'(?:^|\n)MARKER_BENCH_SAMPLE (\{[^\r\n]+\})',raw)]
    assert [r['action'] for r in rows]==['move','resize']
    (a.output/(mode+'-samples.json')).write_text(json.dumps(rows,indent=2)+'\n')
    actions={}
    for row in rows:
        assert len(row['moves'])==a.samples
        expected=['single-tile','tile-rim'] if mode=='tile' else ['outline']
        assert all(p['preview']['mode'] in expected for p in row['moves']), 'runtime preview mode differs from label'
        if a.require_rim and mode=='tile' and row['action']=='resize':assert all(p['preview']['mode']=='tile-rim' for p in row['moves'])
        if a.require_full_move and mode=='tile' and row['action']=='move':assert all(p['preview'].get('colored_cells_per_tile')==1024 and p['preview']['payload_ops']==0 and p['preview']['payload_builds']==0 for p in row['moves'])
        first,last=row['before']['last_render'],row['moves'][-1]['render']
        actions[row['action']]=dict(
            preview_mode=row['moves'][0]['preview']['mode'],
            colored_cells_per_tile=row['moves'][0]['preview'].get('colored_cells_per_tile',64 if row['action']=='move' else 1024),
            coordinate_acceptance=stats([p['accept_ms'] for p in row['moves']]),
            accepted_to_publication=stats([p['accept_to_visible_us']/1000 for p in row['moves']]),
            command_to_completion=stats([p['client_complete_ms'] for p in row['moves']]),
            press=row['press'],release=row['release'],
            motion_work={f:last[f]-first[f] for f in ['footprint_calls_total','plan_builds_total','run_submissions_total']},
            release_publications=row['after']['publications']-row['before_release']['publications'])
        if row['moves'][0]['preview']['mode']=='tile-rim':
            actions[row['action']]['boundary_prepare']=stats([p['preview']['resize_prepare_us']/1000 for p in row['moves']])
            actions[row['action']]['boundary_tiles']=[len(p['preview']['rim_tiles']) for p in row['moves']]
            assert all(p['preview']['payload_ops']==0 for p in row['moves'])
    report['modes'][mode]=actions
    (a.output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print('MODE_DONE',mode,json.dumps(actions),flush=True)
    metrics=[]
    for name,action in actions.items():
        for field,title in [('coordinate_acceptance','좌표 수락'),('command_to_completion','입력→게시 완료')]:
            m=action[field]
            metrics.append(dict(name=mode+' '+name+' '+title,**{k:m[k] for k in ['median_ms','p95_ms','max_ms']},scope='보드 내부, UART/캡처 제외'))
    status(mode+' 지연 측정 완료',metrics)
print('BOARD_MARKER_COMPARISON_DONE',flush=True)
