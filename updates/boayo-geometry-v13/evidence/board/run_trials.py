from pathlib import Path
import time,json,re,shutil
from board_io import command,status,log
r=Path(__file__).parent
end=time.monotonic()+1800
while not (r/'geometry-benchmark-uart.txt').exists():
 if time.monotonic()>end:raise TimeoutError('benchmark incomplete; app trials not started')
 time.sleep(2)
assert 'PASS_RV32_GEOMETRY_STAGES_EXACT' in (r/'geometry-benchmark-uart.txt').read_text()
result={}
for label,old,steps in [('baseline',1,11),('optimized',0,25)]:
 (r/'active-trial.json').write_text(json.dumps(dict(mode=label,started=time.time(),steps=steps,log_start=log.stat().st_size)))
 command(f'BOAYO_PROFILE=1 BOAYO_INIT_TRACE=0 BOAYO_GEOMETRY_V12={old} /tmp/boayo-v13 start',120)
 command('sleep 12; /tmp/boayo-v13 app telemetry; sleep 20',120)
 before=status(label+'-before')
 raw=command(f'BOAYO_MOTION_PROFILE=1 /tmp/boayo-v13 motion 1 0 {steps}',3600)
 (r/(label+'-motion.txt')).write_text(raw)
 assert 'BOAYO_MOTION_COMPLETE' in raw
 entries=[json.loads(x) for x in re.findall(r'BOAYO_MOTION_STEP (\{[^\r\n]+\})',raw)]
 profiles=[json.loads(x) for x in re.findall(r'BOAYO_MOTION_PROFILE (\{[^\r\n]+\})',raw)]
 assert len(entries)==steps and all(x['output_complete'] for x in entries)
 after=status(label+'-after')
 assert after['output']['memory_dma']['errors']==after['output']['memory_dma']['timeouts']==after['output']['poisoned']==0
 appids=lambda s:{w['window_id'] for w in s['windows'] if w.get('workspace',{}).get('role')=='app'}
 assert appids(before)==appids(after)
 result[label]=dict(steps=entries,profiles=profiles,projection=after['projection'])
 (r/'comparison-progress.json').write_text(json.dumps(result,indent=2))
 print('PASS_TRIAL_'+label.upper(),flush=True)
 command('/tmp/boayo-v13 stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 BOAYO_GEOMETRY_V12=0 /tmp/boayo-v13 start',120)
command('sleep 12; /tmp/boayo-v13 app telemetry; sleep 20',120)
status('final-running')
(r/'comparison.json').write_text(json.dumps(dict(result='PASS_PAIRED_FIRST_MOVE_AND_OPTIMIZED_MOTION25',trials=result),indent=2))
print('PASS_BOARD_COMPARISON_AND_RESTORE',flush=True)
