import sys,json
from pathlib import Path
from board_io import command,status
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
root=Path(__file__).parent
upload('D:/temp/haebancheon-motion-v7/board-trial-20261114/apply_delta','/tmp/v11-apply')
upload(str(root/'fix1.delta'),'/tmp/v11-fix1.delta')
command('/tmp/v11-apply /usr/bin/boayo-native /tmp/v11-fix1.delta /tmp/boayo-fix1',300)
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /tmp/boayo-fix1 start',180)
command('sleep 12; /tmp/boayo-fix1 app telemetry',180)
command('sleep 20',60)
raw=command('/tmp/boayo-fix1 profile; sleep 40; /tmp/boayo-fix1 profile',150)
(root/'fix1-normal.txt').write_text(raw)
rows=[]
for line in raw.splitlines():
 try:d=json.loads(line)
 except ValueError:continue
 if 'snapshot_time' in d:rows.append(d)
assert len(rows)==2 and all(x['app_error'] is None and x['render_error'] is None for x in rows)
a,b=rows;n=b['performance']['render_count']-a['performance']['render_count'];assert n>0
report=dict(frames=n,seconds_per_update=(b['snapshot_time']-a['snapshot_time'])/n,app_sha256='d51393051ecfa84b7f332b5b8cc36d9263e45ae5c0a43e972fa9e66f6dbc30b3',deployment='RAM executable over v11 kernel; fixed SD not cold-booted on hardware')
(root/'fix1-normal.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
status('fix1-normal-state')
print('FIX1_STAGED_MEASURED',flush=True)
