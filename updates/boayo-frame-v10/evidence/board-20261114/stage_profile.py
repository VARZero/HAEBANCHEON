from pathlib import Path
import sys,shutil
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
sys.path.insert(0,'D:/temp/haebancheon-dirty-v9/board-trial')
from board_io import command
command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
upload('D:/temp/haebancheon-frame-v10/profile.delta','/tmp/frame-profile.delta')
command('/tmp/d9-apply /usr/bin/boayo-native /tmp/frame-profile.delta /tmp/boayo-frame-profile',300)
command('BOAYO_PROFILE=1 BOAYO_INIT_TRACE=0 /tmp/boayo-frame-profile start',120)
command('sleep 12; /tmp/boayo-frame-profile app telemetry',180)
command('sleep 20',60)
raw=command('/tmp/boayo-frame-profile profile; sleep 30; /tmp/boayo-frame-profile profile',120)
Path('D:/temp/haebancheon-frame-v10/baseline-profile.txt').write_text(raw)
print('PROFILE_MEASURED',flush=True)
