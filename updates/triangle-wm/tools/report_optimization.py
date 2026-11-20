"""Compare unchanged application workloads and shared pixel formats on the board."""
import argparse,json,math,re,statistics
from pathlib import Path
from collections import defaultdict
ap=argparse.ArgumentParser()
ap.add_argument('--case',action='append',required=True,help='FORMAT=UART_LOG')
ap.add_argument('--baseline',required=True);ap.add_argument('--output',required=True);ap.add_argument('--status')
ap.add_argument('--scope',default='Same physical board and application workload; see case labels for service or pixel-format changes. App-call to publication completion, excluding optical latency.')
a=ap.parse_args();baseline=json.loads(Path(a.baseline).read_text(encoding='utf-8'));runs={};display=[]
labels={'initial_content':'최초 콘텐츠','partial_8x8':'8×8 갱신','content_move':'콘텐츠 이동','content_resize':'크기 변경','overlap_move':'3개 창 중 이동','raise':'순서 변경','close':'창 닫기','view_pose':'출력 시점','destroy_owned':'마지막 창 닫기','content_restore_size':'위치·크기 복원','second_content':'두 번째 콘텐츠','third_content':'세 번째 콘텐츠'}
for case in a.case:
    name,path=case.split('=',1);text=Path(path).read_text(errors='replace')
    assert 'BOAYO_TRIANGLE_BENCH_DONE' in text and re.search(r'TRI_\w+_RC:0',text),path
    samples=[json.loads(x) for x in re.findall(r'BOAYO_TRIANGLE_SAMPLE (\{[^\r\n]*\})',text)]
    creates=[json.loads(x) for x in re.findall(r'BOAYO_TRIANGLE_CREATE (\{[^\r\n]*\})',text)]
    poses=[json.loads(x) for x in re.findall(r'BOAYO_TRIANGLE_POSE_SWEEP (\{[^\r\n]*\})',text)]
    assert len(samples) in (13,45),(name,len(samples))
    groups=defaultdict(list)
    for sample in samples:
        assert sample['present_ms']>0 and sample['present_ms']>=sample.get('accept_ms',0)
        groups[sample['name']].append(sample)
    metrics=[]
    for key,group in groups.items():
        values=sorted(s['present_ms'] for s in group)
        m=dict(key=key,n=len(values),median_ms=round(statistics.median(values),3),p95_ms=round(values[math.ceil(.95*len(values))-1],3),max_ms=round(values[-1],3))
        if 'accept_ms' in group[0]:m['accept_median_ms']=round(statistics.median(s['accept_ms'] for s in group),3)
        for field in ('plan_ms','plan_tiles','plan_rejected','plan_fallbacks','render_ms','projection_ms','publish_ms','projected_tiles','texture_upload_bytes','texture_scatter_bytes','texture_evictions','resident_textures','owned_tiles','changed_tiles','content_changed_tiles'):
            data=[s['render'][field] for s in group if field in s.get('render',{})]
            if data:m[field+'_median']=round(statistics.median(data),3)
        for field in ('parse_us','dispatch_us','reply_us','snapshot_us','pixels_us','damage_us','footprint_us'):
            data=[s.get('render',{}).get('request_profile',{}).get(field) for s in group]
            data=[v for v in data if v is not None]
            if data:m['request_'+field+'_median']=round(statistics.median(data),3)
        metrics.append(m)
        display.append({**m,'name':name+' / '+labels.get(key,key),'scope':'앱 호출 → 출력 완료; 같은 FPGA, 광학 지연 제외'})
    runs[name]=dict(log=path,samples=samples,metrics=metrics,creates=creates,pose_sweeps=poses)
report=dict(baseline_file=a.baseline,baseline_metrics=baseline['metrics'],runs=runs,
            scope=a.scope)
Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if a.status:
    p=Path(a.status);s=json.loads(p.read_text(encoding='utf-8'));s['metrics']=display;p.write_text(json.dumps(s,ensure_ascii=False),encoding='utf-8')
for name,run in runs.items():
    print(name,json.dumps(run['metrics'],ensure_ascii=True))
