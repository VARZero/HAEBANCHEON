from pathlib import Path
import sys
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
sys.path.insert(0,'D:/temp/haebancheon-dirty-v9/board-trial')
from board_io import command,status
root=Path('D:/temp/haebancheon-frame-v10')
command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
upload(str(root/'candidate.delta'),'/tmp/frame-candidate.delta')
command('/tmp/d9-apply /tmp/boayo-frame-profile /tmp/frame-candidate.delta /tmp/boayo-frame-v10',300)
command('BOAYO_PROFILE=1 BOAYO_INIT_TRACE=0 /tmp/boayo-frame-v10 start',120)
command('sleep 12; /tmp/boayo-frame-v10 app telemetry',180)
command('sleep 20',60)
raw=command('/tmp/boayo-frame-v10 profile; sleep 30; /tmp/boayo-frame-v10 profile',120)
(root/'candidate-profile.txt').write_text(raw)
raw=command('/tmp/d9-peek 1; sleep 30; /tmp/d9-peek 1',120)
(root/'candidate-profile-quiet.txt').write_text(raw)
status('frame-v10-profile-state')
print('CANDIDATE_PROFILE_COMPLETE',flush=True)
