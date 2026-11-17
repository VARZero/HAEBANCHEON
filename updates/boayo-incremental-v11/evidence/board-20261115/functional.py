import json,re
from pathlib import Path
from board_io import command,status
r=Path(__file__).parent
raw=command('/tmp/boayo-fix1 motion 1 0',900)
(r/'motion.txt').write_text(raw)
steps=[]
for line in raw.splitlines():
    if line.startswith('BOAYO_MOTION_STEP '):
        steps.append(json.loads(line.split(' ',1)[1]))
assert len(steps)==25 and all(x['output_complete'] for x in steps)
assert 'BOAYO_MOTION_COMPLETE device-acknowledged' in raw
state=status('motion-restored-state')
assert state['output']['memory_dma']['errors']==0
assert state['output']['memory_dma']['timeouts']==0
(r/'functional-result.json').write_text(json.dumps({
    'result':'PASS_DEVICE_ACKNOWLEDGED', 'steps':steps,
    'scope':'Physical KFB; v11-fix1 app in RAM over v11 kernel. Mixed pitch/yaw/roll, move, resize, overlap, focus, close and restore. HDMI pixel correctness is not established by acknowledgements.',
    'app_error':state['app_error'],'render_error':state['render_error'],
    'dma':state['output']['memory_dma']},indent=2))
print('FUNCTIONAL_COMPLETE_RESTORED',flush=True)
