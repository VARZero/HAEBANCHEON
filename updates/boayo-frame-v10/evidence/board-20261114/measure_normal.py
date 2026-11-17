from pathlib import Path
import sys,shutil
sys.path.insert(0,'D:/temp/haebancheon-dirty-v9/board-trial')
from board_io import command,status
r=Path('D:/temp/haebancheon-frame-v10')
command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
command('BOAYO_PROFILE=0 BOAYO_INIT_TRACE=0 /tmp/boayo-frame-v10 start',120)
command('sleep 12; /tmp/boayo-frame-v10 app telemetry',180)
command('sleep 20',60)
raw=command('/tmp/d9-peek 1; sleep 40; /tmp/d9-peek 1',120)
(r/'candidate-normal-quiet.txt').write_text(raw)
raw=command('/tmp/boayo-frame-v10 profile',60);(r/'candidate-normal-profile.txt').write_text(raw)
state=status('frame-v10-normal-state')
(r/'candidate-normal-state.json').write_text(__import__('json').dumps(state,indent=2))
shutil.copy2('D:/temp/haebancheon-dirty-v9/board-trial/hdmi_live/latest.png',r/'telemetry-normal.png')
shutil.copy2('D:/temp/haebancheon-dirty-v9/board-trial/hdmi_live/status.json',r/'telemetry-normal-capture.json')
print('NORMAL_MEASURED_V10_RUNNING_RAM_ONLY',flush=True)
