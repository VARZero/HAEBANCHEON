from pathlib import Path
import time,json,sys
from board_io import command,status
root=Path(__file__).parent
end=time.monotonic()+7200
while not (root/'result-cull.json').exists():
 if time.monotonic()>end:raise TimeoutError('functional verification not completed; benchmark not started')
 time.sleep(2)
assert json.loads((root/'result-cull.json').read_text())['result']=='PASS_DEVICE_ACKNOWLEDGED'
command('/tmp/boayo-v12c stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
upload(str(root/'bench.delta'),'/tmp/mask-bench.delta')
command('/tmp/v11-apply /tmp/boayo-v12c /tmp/mask-bench.delta /tmp/mask-bench',300)
r=dict(uart=command('/tmp/mask-bench',1200),rc=0)
(root/'mask-benchmark.json').write_text(json.dumps(r,indent=2))
assert 'PASS_RV32_MASK_BENCHMARK' in r['uart']
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /tmp/boayo-v12c start',120)
command('sleep 12; /tmp/boayo-v12c app telemetry; sleep 20',120)
status('final-running')
print('PASS_MASK_BENCH_AND_SERVICE_RESTORED',flush=True)

