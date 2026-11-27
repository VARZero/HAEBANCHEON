"""Boot the actual delivered kernel/RV32 apps. QEMU does not emulate board SPI."""
import argparse,importlib.util,json,struct,subprocess,tempfile
from pathlib import Path
repo=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('smoke',repo/'software/userland/tests/qemu_smoke.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
ap=argparse.ArgumentParser();ap.add_argument('--image',type=Path,default=Path('/var/tmp/triangle-keyboard-v16/release/HAEBANCHEON-keyboard-v16.img'));ap.add_argument('--output',type=Path,default=Path('/var/tmp/triangle-keyboard-v16/qemu'));a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
layout=s.image.inspect_image(a.image)
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp);loaders=[]
 with a.image.open('rb') as f:
  f.seek(s.image.HEADER+512)
  for i,seg in enumerate(layout['segments']):
   p=root/str(i);p.write_bytes(f.read(seg['sectors']*512))
   if i!=2:loaders+=['-device',f'loader,file={p},addr={seg["address"]},force-raw=on']
 dtb=root/'qemu.dtb';dts=root/'qemu.dts';dts.write_text(s.qemu_dts());subprocess.run(['dtc','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
 entry=root/'entry';entry.write_bytes(struct.pack('<4I',0x00000513,0x87f005b7,0x800002b7,0x00028067))
 loaders+=['-device',f'loader,file={dtb},addr=0x87f00000,force-raw=on','-device',f'loader,file={entry},addr=0x80200000,force-raw=on,cpu-num=0']
 cmd=['qemu-system-riscv32','-machine','virt','-cpu','rv32,c=false,f=false,d=false,v=false,zfa=false,zawrs=false','-m','1G','-smp','1','-bios','none','-display','none','-monitor','none','-serial','stdio','-no-reboot','-drive',f'file={a.image},if=none,format=raw,id=sd,snapshot=on','-device','virtio-blk-device,drive=sd,bus=virtio-mmio-bus.0']+loaders
 with (a.output/'uart.log').open('wb') as log:
  c=s.Console(cmd,log)
  try:
   c.until(rb'haebancheon# ',timeout=120)
   c.run('test "$(uname -r)" = 6.12.0-haebancheon-keyboard && test -c /dev/uinput && test -d /sys/bus/platform/drivers/haebancheon-peripherals',timeout=30)
   result=c.run('KEYBOARD_GUEST=1 python3 /usr/share/triangle-wm/tests/test_keyboard_navigation.py /usr/bin/triangle-boayo-fixed /usr/bin/triangle-examples /usr/bin/boayo-input',timeout=300)
   assert b'PASS_KEYBOARD_NAVIGATION' in result
   c.run('triangle-fixed-start mock && test -x /run/triangle-wm-runtime/boayo-input && boayo-input --key Home',timeout=120)
   c.run('sleep 2; triangle-boayo-fixed --ctl \'{"op":"get_state"}\' | grep "BoAYO launcher"',timeout=120)
   c.run('triangle-fixed-start stop',timeout=30)
  finally:c.close()
report={'result':'PASS_KEYBOARD_QEMU_IMAGE','image_sha256':s.image.sha(a.image),'kernel_release':'6.12.0-haebancheon-keyboard','actual_rv32_apps':True,'tty_pty_navigation':True,'stdin_ctrl_c_restoration':True,'home_launcher':True,'staged_startup':True,'hardware_tested':False,'scope':'Actual RV32 binaries on delivered Linux kernel, TTY/PTTY and binary WM input. QEMU virt device tree only; board MMIO SPI/I2C/UART, ESP32, HDMI and physical latency are not emulated.'}
(a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
