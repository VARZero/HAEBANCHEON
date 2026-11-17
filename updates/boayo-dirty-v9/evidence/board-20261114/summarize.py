from pathlib import Path
import json,re,statistics,hashlib
r=Path(__file__).parent
def rows(name):return [json.loads(m) for m in re.findall(r'BENCH_SAMPLE (\{[^\r\n]+\})',(r/(name+'.txt')).read_text())]
def analyze(name):
 samples=rows(name);unique={s['renders']:s for s in samples};s=list(unique.values());assert len(s)>=2
 return dict(samples=len(samples),distinct_completed_frames=len(s),compose_median_ms=statistics.median(x['compose_ms'] for x in s),compose_min_ms=min(x['compose_ms'] for x in s),compose_max_ms=max(x['compose_ms'] for x in s),dma_errors=max(x['dma_errors'] for x in s))
def quiet(name):
 s=rows(name);a,b=s[0],s[-1];n=b['renders']-a['renders'];dt=b['uptime']-a['uptime'];assert n>0 and dt>0
 return dict(completed_updates=n,snapshot_interval_seconds=dt,approx_seconds_per_update=dt/n,direct_upload_bytes_per_update=(b['dma_upload_bytes']-a['dma_upload_bytes'])/n,pool_full_upload_delta=None if a['full_uploads'] is None else b['full_uploads']-a['full_uploads'],pool_partial_upload_delta=None if a['partial_uploads'] is None else b['partial_uploads']-a['partial_uploads'])
old=analyze('v8-telemetry-samples');new=analyze('v9-telemetry-samples')
result=dict(hardware=True,scenario='One continuously updating Telemetry window, same SD/kernel/bitstream, prior executable staged in RAM',baseline_version='view-v8',candidate_version='dirty-v9',old=old,new=new,compose_reduction_percent=(1-new['compose_median_ms']/old['compose_median_ms'])*100,quiet_v8=quiet('v8-quiet'),quiet_v9_before=quiet('v9-quiet'),quiet_v9_restored=quiet('v9-restored-quiet'),limitations=['Small real-board sample, not a universal UI speedup','Quiet intervals use cached completed-render snapshots; per-update interval is approximate','Composition duration excludes application update work; transfer staging excluded','HDMI visual status is reported separately'])
result['bitstream_sha256']=hashlib.file_digest(Path('D:/temp/haebancheon-l1-16k-dma3/HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit').open('rb'),'sha256').hexdigest()
result['image_sha256']=json.loads((r.parent/'evidence/image-manifest.json').read_text())['sha256']
result['baseline_sha256']=hashlib.file_digest(Path('D:/temp/haebancheon-view-v8/boayo-v8').open('rb'),'sha256').hexdigest()
result['candidate_sha256']=json.loads((r.parent/'evidence/artifact-audit.json').read_text())['app_sha256']
(r/'performance-comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
