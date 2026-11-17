from pathlib import Path
import json,re
r=Path(__file__).parent
a=json.loads((r/'baseline-profile.json').read_text());b=json.loads((r/'candidate-profile.json').read_text())
result={'scope':'Physical KFB board; same kernel, SD, FPGA; warmed Telemetry updates; profile enabled both runs','baseline':a,'candidate':b,'interval_reduction_percent':(1-b['snapshot_interval_ms']/a['snapshot_interval_ms'])*100}
for label,file in [('profile_quiet','candidate-profile-quiet.txt'),('normal_quiet','candidate-normal-quiet.txt')]:
 if not (r/file).exists():continue
 rows=[json.loads(s) for s in re.findall(r'BENCH_SAMPLE (\{[^\r\n]+\})',(r/file).read_text())]
 assert len(rows)==2 and all(x['dma_errors']==0 and x['app_error'] is None and x['render_error'] is None for x in rows)
 x,y=rows;n=y['renders']-x['renders'];assert n>0
 result[label]={'frames':n,'seconds_per_update':(y['uptime']-x['uptime'])/n,'direct_upload_bytes_per_update':(y['pool_upload_bytes']-x['pool_upload_bytes'])/n,'partial_uploads':y['partial_uploads']-x['partial_uploads'],'full_uploads':y['full_uploads']-x['full_uploads'],'samples':rows}
(r/'comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k not in ('baseline','candidate')},indent=2))
