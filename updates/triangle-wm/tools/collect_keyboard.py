"""검사를 통과한 키 입력 개발본과 SD 이미지를 모은다. 보드에는 접속하지 않는다."""
import argparse,hashlib,json,re,shutil,subprocess
from datetime import datetime,timezone,timedelta
from pathlib import Path
repo=Path(__file__).resolve().parents[3]
ap=argparse.ArgumentParser();ap.add_argument('--build',type=Path,default=Path('/var/tmp/triangle-keyboard-v16'));ap.add_argument('--output',type=Path,default=Path('/mnt/d/temp/haebancheon-keyboard-v16'));a=ap.parse_args();b=a.build
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
manifest=json.loads((b/'release/manifest.json').read_text());boot=json.loads((b/'qemu/result.json').read_text());features=json.loads((b/'userland/features.json').read_text());image=b/'release/HAEBANCHEON-keyboard-v16.img'
assert manifest['version'] in ('fixed-wm-keyboard-v16','fixed-wm-keyboard-v16a','fixed-wm-keyboard-v16b','fixed-wm-keyboard-v16c') and manifest['kernel_release']=='6.12.0-haebancheon-keyboard'
assert boot['result']=='PASS_KEYBOARD_QEMU_IMAGE' and not boot['hardware_tested']
assert sha(image)==manifest['sha256']==boot['image_sha256']
assert sha(b/'firmware/Image')==manifest['kernel_sha256']
assert features['binaries']==manifest['binaries'] and all(sha(b/'userland'/n)==h for n,h in features['binaries'].items())
config=(b/'kernel/.config').read_text()
for name in ['INPUT','INPUT_EVDEV','INPUT_UINPUT','SERIAL_8250','SPI','SPI_SPIDEV','I2C','I2C_CHARDEV','HAEBANCHEON_PERIPHERALS']:assert f'CONFIG_{name}=y\n' in config,name
assert '# CONFIG_SERIAL_OF_PLATFORM is not set' in config
assert sha(repo/'updates/triangle-wm/linux/haebancheon_peripherals.c')==sha(b/'linux/drivers/misc/haebancheon_peripherals.c')
dtb=b/'images/boot/gen3.dtb'
for node,compatible in [('serial@10005000','oryukdo,uart-poll-v1'),('spi@10006000','oryukdo,spi-v1'),('i2c@10007000','oryukdo,i2c-v1')]:
 assert subprocess.check_output(['fdtget',str(dtb),'/soc/'+node,'compatible'],text=True).strip()==compatible
assert 'spi-cpha' in subprocess.check_output(['fdtget','-p',str(dtb),'/soc/spi@10006000/keyboard@0'],text=True).split()
native=Path('/var/tmp/triangle-keyboard-native-tests.log').read_text()
for marker in ['PASS_KEY_TRANSPORT','PASS_KEYBOARD_NAVIGATION','PASS_SPI_KEYBOARD','PASS_APP_EVENTS','PASS_NATIVE_EXAMPLES','PASS_DESKTOP_APPS','KEYBOARD_NATIVE_VALIDATION_PASS']:assert marker in native,marker
for name,marker in [('tty.log','PASS_KEYBOARD_NAVIGATION'),('spi.log','PASS_SPI_KEYBOARD')]:assert marker in (b/'validation'/name).read_text()
core=(b/'userland/fixed-core-symbols.txt').read_text();assert not re.search(r'(__.*[ds]f[23]|sin|cos|atan|sqrt|pow)',core)
try:
 baseline_diff=subprocess.check_output(['git','diff','--name-only','--','variants/l1-16k-dma3-lru-ram'],cwd=repo,text=True,stderr=subprocess.PIPE)
except subprocess.CalledProcessError:
 # Windows worktree .git files contain a drive-letter path; use native Git there.
 pointer=repo/'.git'
 if not pointer.is_file() or not re.search(r'gitdir:\s*[A-Za-z]:',pointer.read_text()):raise
 native_git=shutil.which('git.exe');assert native_git,'Windows worktree needs native git.exe'
 windows_repo=subprocess.check_output(['wslpath','-w',str(repo)],text=True).strip()
 baseline_diff=subprocess.check_output([native_git,'-C',windows_repo,'diff','--name-only','--','variants/l1-16k-dma3-lru-ram'],text=True)
assert not baseline_diff.strip(),'baseline RTL changed'
sources=['src/boayo_input.cpp','src/key_transport.hpp','src/esp32_key_queue.hpp','src/keyboard_navigation.inc','src/boayo_service.cpp','src/app_ui.hpp','src/example_apps.hpp','src/desktop_apps.hpp','src/ui_input.inc','src/ui_render.inc','src/triangle_wm.cpp','linux/haebancheon_peripherals.c','linux/peripherals.dtsi','tools/build_fixed.sh','tools/build_keyboard_kernel.sh','tools/package_keyboard.sh','tools/package_fixed.py','tools/triangle-fixed-start','tools/validate_keyboard.sh','tools/test_keyboard_image.py','tests/test_keyboard_navigation.py','tests/test_spi_keyboard.py','tests/spi_keyboard_fixture.c','tests/check_key_transport.cpp','tests/check_large_controls.cpp','tests/example_test_support.py','README.md','KEYBOARD.ko.md']
result={'result':'PASS_KEYBOARD_V16_OFFLINE','created_kst':datetime.now(timezone(timedelta(hours=9))).isoformat(),'version':manifest['version'],'kernel_release':manifest['kernel_release'],'image':image.name,'image_sha256':manifest['sha256'],'bytes':image.stat().st_size,'kernel_sha256':manifest['kernel_sha256'],'board_dtb_sha256':sha(dtb),'kernel_config_sha256':sha(b/'kernel/.config'),'binaries':features['binaries'],'sources_sha256':{n:sha(repo/'updates/triangle-wm'/n) for n in sources},'native_checks':['real PTY/stdin/Ctrl+C restoration','six-key spatial navigation; captionless Home; ordinary app close','home launcher launch/reuse and dashboard launched from home','IPC retry dedup','SPI mode1 CRC ACK retransmit reboot session','input_event release/repeat/SYN_DROPPED','existing app events and desktop applications','cached close mask keyboard focus/unfocus'],'actual_rv32_qemu_boot':boot,'fixed_core_float_audit':True,'fpga_bitstream_changed':False,'board_contacted':False,'hardware_tested':False,'peripheral_hardware_tested':False,'hardware_latency_measured':False,'input_autostart':False,'scope_ko':'기존 FPGA 유지. 리눅스 드라이버는 실제 커널 빌드 및 DT 등록 검사. 실제 RV32 앱과 TTY는 QEMU 검사. SPI는 시스템 호출 모의 장치 검사이며 실물 SoC·ESP32·UART·I2C·HDMI 성능 검사는 아님.'}
a.output.mkdir(parents=True,exist_ok=True);target=a.output/image.name;shutil.copyfile(image,target);assert sha(target)==manifest['sha256']
for source,dest in [(b/'release/manifest.json','image-manifest.json'),(b/'qemu/result.json','qemu-result.json'),(b/'kernel/.config','linux.config'),(dtb,'gen3.dtb'),(repo/'updates/triangle-wm/KEYBOARD.ko.md','사용방법.md')]:shutil.copyfile(source,a.output/dest)
for name in ['src/key_transport.hpp','src/esp32_key_queue.hpp','linux/haebancheon_peripherals.c','linux/peripherals.dtsi']:
 target=a.output/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(repo/'updates/triangle-wm'/name,target)
reports=a.output/'검사결과';reports.mkdir(exist_ok=True)
for source,name in [(Path('/var/tmp/triangle-keyboard-native-tests.log'),'native.log'),(b/'validation/tty.log','tty.log'),(b/'validation/spi.log','spi.log'),(b/'qemu/uart.log','qemu-uart.log'),(b/'userland/fixed-core-symbols.txt','fixed-core-symbols.txt')]:shutil.copyfile(source,reports/name)
result['reports_sha256']={p.name:sha(p) for p in reports.iterdir() if p.is_file()}
(a.output/'validation.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
folder=repo/'updates/triangle-wm/results';(folder/'keyboard-validation.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');(folder/'keyboard-image.json').write_text(json.dumps(manifest,indent=2)+'\n');(folder/'keyboard-boot.json').write_text(json.dumps(boot,indent=2)+'\n')
print(json.dumps({'result':result['result'],'image':str(a.output/image.name),'sha256':manifest['sha256'],'hardware_tested':False},indent=2))
