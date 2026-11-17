import sys
from pathlib import Path
sys.path.insert(0,'D:/temp/haebancheon-motion-v7/board-trial-20261114')
from upload_raw import upload
sys.path.insert(0,str(Path(__file__).parent))
from board_io import command,root
upload(root/'peek','/tmp/d9-peek')
samples=command('/tmp/d9-peek 12',180)
(root/'v9-telemetry-samples.txt').write_text(samples)
upload(Path('D:/temp/haebancheon-motion-v7/board-trial-20261114/apply_delta'),'/tmp/d9-apply')
upload(root/'baseline-from-v9.delta','/tmp/d9-baseline.delta')
command('/tmp/d9-apply /usr/bin/boayo-native /tmp/d9-baseline.delta /tmp/boayo-v8-baseline',180)
print('BASELINE_STAGED_RAM_ONLY',flush=True)
