"""Summarize target-clock measurements without treating commit acceptance as display."""
import argparse,json,math,re,statistics
from collections import defaultdict
from pathlib import Path

ap=argparse.ArgumentParser();ap.add_argument('logs',nargs='+');ap.add_argument('--output',required=True);ap.add_argument('--status')
a=ap.parse_args();samples=[];legacy=[];create=[];sweeps=[];complete=False
for log in a.logs:
    text=Path(log).read_text(errors='replace')
    complete |= 'BOAYO_TRIANGLE_BENCH_DONE' in text
    for line in text.splitlines():
        for marker,target in [('BOAYO_TRIANGLE_SAMPLE',samples),('BOAYO_SHARED_BENCH',legacy),('BOAYO_TRIANGLE_CREATE',create),('BOAYO_TRIANGLE_POSE_SWEEP',sweeps)]:
            match=re.search(marker+r' (\{.*\})',line)
            if match: target.append(json.loads(match[1]))
assert complete and samples and sweeps,'benchmark incomplete'
labels={'initial_content':'최초 앱 콘텐츠','partial_8x8':'8×8 부분 갱신','content_move':'콘텐츠 창 이동','content_resize':'콘텐츠 창 크기 변경',
        'content_restore_size':'원래 위치·크기 복원','second_content':'두 번째 앱 콘텐츠','third_content':'세 번째 앱 콘텐츠','overlap_move':'겹친 콘텐츠 창 이동',
        'raise':'창 순서 변경','close':'겹친 창 닫기','view_pose':'출력 시점 변경','destroy_owned':'앱이 자기 창 닫기'}
groups=defaultdict(list)
for sample in samples: groups[sample['name']].append(sample)
metrics=[]
for name,group in groups.items():
    values=sorted(s['present_ms'] for s in group)
    metric=dict(name='보아요 / '+labels.get(name,name),key=name,n=len(values),median_ms=round(statistics.median(values),3),
                p95_ms=round(values[math.ceil(len(values)*.95)-1],3),max_ms=round(values[-1],3),scope='앱 호출 → 출력 완료 응답 (광학 지연 제외)')
    accepted=sorted(s['accept_ms'] for s in group if 'accept_ms' in s)
    if accepted: metric['accept_median_ms']=round(statistics.median(accepted),3)
    metrics.append(metric)
for item in legacy:
    metrics.append(dict(name=f"기존 보아요 실행 파일 / {item['edge']}×{item['edge']} 갱신",n=item['rounds'],median_ms=round(item['median_ms'],3),
                        p95_ms=round(item['p95_ms'],3),max_ms=None,scope='기존 commit → wait_present 경로'))
report=dict(scope='Physical board, persistent BoAYO Client and RGB24 shared surfaces; no optical latency',logs=a.logs,
            metrics=metrics,samples=samples,create=create,pose_sweeps=sweeps,legacy_benchmarks=legacy,complete=complete)
Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if a.status:
    path=Path(a.status);status=json.loads(path.read_text(encoding='utf-8'));status['metrics']=metrics
    path.write_text(json.dumps(status,ensure_ascii=False),encoding='utf-8')
print(json.dumps(metrics,ensure_ascii=False,indent=2))
