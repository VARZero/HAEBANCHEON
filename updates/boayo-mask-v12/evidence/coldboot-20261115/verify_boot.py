from board_io import command,wait
from pathlib import Path
root=Path(__file__).parent
wait(rb'oryukdo# ',0,900)
raw=command('cat /etc/oryukdo-image; uname -r; cat /proc/mounts; ps',90)
(root/'boot-identity.txt').write_text(raw)
assert 'HAEBANCHEON-BoAYO-mask-v12' in raw and '6.12.0-haebancheon-mask12' in raw
print('PASS_V12_SD_LINUX_SHELL',flush=True)
