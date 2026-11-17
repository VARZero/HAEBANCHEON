from board_io import *
start=json.loads((root/'uart_start.json').read_text())['offset']
wait(rb'oryukdo# ',start,600)
identity=command('uname -r; cat /etc/oryukdo-image; cat /proc/mounts; ls -l /usr/bin/boayo-native')
assert '6.12.0-haebancheon-dirty9' in identity and 'HAEBANCHEON-BoAYO-dirty-v9' in identity
assert '/dev/mmcblk0p2 /data ext4 rw' in identity
command('boayo-start',300)
wait(rb'event=output.present.done',start,300)
s=status('launcher-state')
assert 'texture_pool' in s['projection']
print('PASS_BOARD_V9_IDENTITY_LAUNCHER',flush=True)
