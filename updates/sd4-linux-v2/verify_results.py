"""Bind the saved v2 results to the driver and optionally the delivered image."""
from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parent
ev=root/'evidence'
result=json.loads((ev/'result.json').read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
driver=sha(root/'oryukdo_sd.c')
assert driver==result['driver_sha256']
log=(ev/'driver_test.log').read_text()
assert '49 cases' in log and 'driver_sha256='+driver in log
for name in ['package.json','image_manifest.json','qemu_result.json']:
 obj=json.loads((ev/name).read_text())
 assert obj.get('image_sha256',obj.get('sha256'))==result['image_sha256'],name
assert json.loads((ev/'qemu_result.json').read_text())['result']=='PASS_QEMU_ONLY'
assert 'PASS native SD boot initialization' in (ev/'actual_loader.log').read_text()
assert 'PASS boot-to-runtime ownership' in (ev/'actual_loader.log').read_text()
assert not result['bitstream_changed'] and result['result']=='PASS_LOCAL_HARDWARE_PENDING'
if len(sys.argv)>1: assert sha(Path(sys.argv[1]))==result['image_sha256']
board_path=ev/'v2_board_result.json'
if board_path.exists():
 board=json.loads(board_path.read_text())
 assert board['selected_image_sha256']==result['image_sha256']
 assert sha(ev/'v2_board_uart.log')==board['uart_snapshot_sha256']
 assert board['result']=='PASS_BOARD_SHELL_SD_EXT4_RW_REMOUNT'
 assert board['file_re_read_compared_bytes']==262144
 print('PASS v2 artifact identity and saved board 256KiB read/write/remount evidence; power cycle not tested')
else:
 print('PASS v2 driver/result/image identity; physical MMC/ext4 retest remains pending')
