from board_io import command,root
from upload_raw import upload
import subprocess,sys
command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',120)
subprocess.run([sys.executable,'-X','utf8',str(root/'install_receiver.py')],check=True)
upload('D:/temp/haebancheon-motion-v7/board-trial-20261114/apply_delta','/tmp/v14-apply')
upload(str(root/'reference.delta'),'/tmp/reference.delta')
command('/tmp/v14-apply /usr/bin/boayo-native /tmp/reference.delta /tmp/boayo-reference',300)
print('PASS_REFERENCE_STAGED_IN_RAM',flush=True)
