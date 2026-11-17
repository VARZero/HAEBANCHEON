from pathlib import Path
import sys,time,json,re
from board_io import command,status,log,wait
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
r=Path(__file__).parent
upload(str(r/'mask12.delta'),'/tmp/mask12c.delta')
command('/tmp/v11-apply /tmp/boayo-fix1 /tmp/mask12c.delta /tmp/boayo-v12c',300)
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /tmp/boayo-v12c start',120)
command('sleep 12; /tmp/boayo-v12c app telemetry; sleep 20',120)
status('before-motion-cull')
start=log.stat().st_size
command('(/tmp/boayo-v12c motion 1 0; echo MASK12C_MOTION_EXIT:$?) < /dev/null & echo MASK12C_LAUNCHED:$!',60)
raw=wait(rb'\nMASK12C_MOTION_EXIT:([0-9]+)\r*\n',start,7200).decode(errors='replace')
(r/'motion-cull.txt').write_text(raw)
rc=int(re.search(r'\nMASK12C_MOTION_EXIT:(\d+)',raw)[1]);assert rc==0,raw[-1000:]
steps=[json.loads(x) for x in re.findall(r'BOAYO_MOTION_STEP (\{[^\r\n]+\})',raw)]
assert len(steps)==25 and all(s['output_complete'] for s in steps)
state=status('after-motion-cull')
assert state['output']['memory_dma']['errors']==state['output']['memory_dma']['timeouts']==0
(r/'result-cull.json').write_text(json.dumps(dict(result='PASS_DEVICE_ACKNOWLEDGED',steps=steps,
 app_sha256='c6e9114ca5398f84e0c7bc2522bda71be7f064a6781e950f4bd20e6ecca0dc51',
 scope='Physical v12 final app in RAM over v11 kernel; unchanged bitstream; 25 motion stages and restore. HDMI visual evidence separate.',
 projection=state['projection'],dma=state['output']['memory_dma']),indent=2))
print('PASS_BOARD_MOTION25_RESTORED',flush=True)
