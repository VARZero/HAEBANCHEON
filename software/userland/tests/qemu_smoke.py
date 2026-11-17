#!/usr/bin/env python3
"""Boot the image's OpenSBI/kernel with a QEMU-only DT and virtio disk.

The board DT in the shipped image is never modified. This does not emulate
ORYUKDO RTL, native SD/DMA, MIG or BOSIO/HDMI.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import selectors
import struct
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('image', HERE.parent / 'make_image.py')
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


class Console:
    def __init__(self, command, log):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, bufsize=0)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.log = log
        self.buffer = bytearray()

    def until(self, pattern, timeout=300):
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, self.buffer)
            if match:
                result = bytes(self.buffer[:match.end()])
                del self.buffer[:match.end()]
                return result
            if not self.selector.select(max(0, deadline - time.monotonic())):
                raise TimeoutError(f'Console timeout: {pattern!r}; see UART log')
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise RuntimeError('QEMU exited before expected output')
            self.log.write(chunk)
            self.log.flush()
            self.buffer.extend(chunk)
            if b'Kernel panic' in self.buffer or b'Illegal instruction' in self.buffer:
                raise RuntimeError('Kernel panic or unsupported ISA; see UART log')

    def run(self, command, timeout=300):
        # Match an actual result line, not the terminal echo of the command.
        self.process.stdin.write((command + "; printf '\\n__RESULT:%s\\n' $?\n").encode())
        reply = self.until(rb'\n__RESULT:[0-9]+\r*\n', timeout)
        if not re.search(rb'\n__RESULT:0\r*\n', reply):
            raise RuntimeError(f'Command failed: {command}\n{reply.decode(errors="replace")}')
        self.until(rb'haebancheon# ')
        print('PASS_QEMU ' + command, flush=True)
        return reply

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.selector.close()


def qemu_dts():
    # Match QEMU virt addresses. Native board peripherals are deliberately
    # absent, and both test-only differences are visible in this source.
    return '''/dts-v1/;
/ {
 #address-cells = <1>; #size-cells = <1>;
 compatible = "riscv-virtio";
 chosen { bootargs = "earlycon=sbi console=hvc0 nokaslr lpj=1000000 rdinit=/init haebancheon.root=/dev/vda2"; };
 memory@80000000 { device_type = "memory"; reg = <0x80000000 0x40000000>; };
 reserved-memory {
  #address-cells = <1>; #size-cells = <1>; ranges;
  dma-pool@bd000000 { reg = <0xbd000000 0x01000000>; no-map; };
  framebuffer@bf800000 { reg = <0xbf800000 0x00800000>; no-map; };
 };
 cpus {
  #address-cells = <1>; #size-cells = <0>; timebase-frequency = <10000000>;
  cpu@0 { device_type = "cpu"; compatible = "riscv"; reg = <0>; status = "okay";
   riscv,isa = "rv32ima_zicsr_zifencei"; mmu-type = "riscv,sv32";
   intc: interrupt-controller { #interrupt-cells = <1>; interrupt-controller; compatible = "riscv,cpu-intc"; };
  };
 };
 soc {
  #address-cells = <1>; #size-cells = <1>; compatible = "simple-bus"; ranges;
  uart@10000000 { compatible = "ns16550a"; reg = <0x10000000 0x100>; clock-frequency = <3686400>; };
  timer@200bff8 { compatible = "riscv,aclint-mtimer"; reg = <0x200bff8 8>, <0x2004000 8>; interrupts-extended = <&intc 7>; };
  plic: interrupt-controller@c000000 { compatible = "riscv,plic0"; reg = <0xc000000 0x400000>;
   #address-cells = <0>; #interrupt-cells = <1>; interrupt-controller; riscv,ndev = <4>;
   interrupts-extended = <&intc 11>, <&intc 9>;
  };
  virtio@10001000 { compatible = "virtio,mmio"; reg = <0x10001000 0x1000>; interrupt-parent = <&plic>; interrupts = <1>; };
 };
};
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--expect-examples', action='store_true',
                        help='also run hello/hello-python installed through APP_OVERLAY')
    args = parser.parse_args()
    layout = image.inspect_image(args.image)
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'result': 'RUNNING', 'hardware': False, 'rtl': False,
              'image_sha256': image.sha(args.image), 'checks': [],
              'differences': ['QEMU-only DT: 10MHz timer, virtio disk, no native board peripherals']}
    try:
        with tempfile.TemporaryDirectory(prefix='haeb-qemu-') as temp:
            root = Path(temp)
            disk = root / 'sd.img'
            subprocess.run(['cp', '--reflink=auto', '--sparse=always', str(args.image), str(disk)], check=True)
            base_command = ['qemu-system-riscv32', '-machine', 'virt', '-cpu', 'rv32,c=false,f=false,d=false,v=false,zfa=false,zawrs=false',
                       '-m', '1G', '-smp', '1', '-bios', 'none', '-display', 'none', '-monitor', 'none',
                       '-serial', 'stdio', '-no-reboot']
            disk_options = ['-drive', f'file={disk},if=none,format=raw,id=sd',
                            '-device', 'virtio-blk-device,drive=sd,bus=virtio-mmio-bus.0']
            loaders = []
            with args.image.open('rb') as src:
                src.seek(image.HEADER + 512)
                for index, segment in enumerate(layout['segments']):
                    payload = root / f'payload{index}'
                    payload.write_bytes(src.read(segment['sectors'] * 512))
                    if index != 2:
                        loaders += ['-device', f'loader,file={payload},addr={segment["address"]},force-raw=on']
            dts = args.output / 'qemu.dts'
            dts.write_text(qemu_dts())
            dtb = root / 'qemu.dtb'
            subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(dtb), str(dts)], check=True)
            loaders += ['-device', f'loader,file={dtb},addr=0x87f00000,force-raw=on']
            entry = root / 'entry'
            entry.write_bytes(struct.pack('<4I', 0x00000513, 0x87f005b7, 0x800002b7, 0x00028067))
            loaders += ['-device', f'loader,file={entry},addr=0x80200000,force-raw=on,cpu-num=0']
            command = base_command + disk_options + loaders
            for run in range(2):
                with (args.output / f'uart-{run}.log').open('wb') as log:
                    console = Console(command, log)
                    try:
                        console.until(rb'haebancheon# ')
                        if run == 0:
                            checks = ["grep ' / ext4 ' /proc/mounts", 'cat /etc/oryukdo-image',
                                      "grep ' /tmp tmpfs ' /proc/mounts && grep ' /run tmpfs ' /proc/mounts && grep ' /dev/pts devpts ' /proc/mounts",
                                      'free -m; df -h / /tmp /data',
                                      'userland-check',
                                      "echo PERSIST_OK > /data/persistence-test; sync"]
                            if args.expect_examples:
                                checks += ['hello', 'hello-python']
                        else:
                            checks = ["test $(cat /data/persistence-test) = PERSIST_OK",
                                      'rm /data/persistence-test; sync']
                        for check in checks:
                            console.run(check, timeout=900)
                            report['checks'].append(check)
                    finally:
                        console.close()
            # Missing SD must retain a usable recovery shell with no formatting.
            recovery = base_command + loaders
            with (args.output / 'uart-recovery.log').open('wb') as log:
                console = Console(recovery, log)
                try:
                    console.until(rb'entering recovery shell')
                    console.process.stdin.write(b'echo RECOVERY_OK\n')
                    console.until(rb'\nRECOVERY_OK\r*\n')
                    report['checks'].append('missing disk: usable recovery shell')
                finally:
                    console.close()
        report['result'] = 'PASS_QEMU_ONLY'
    except BaseException as error:
        report.update(result='FAIL', error=repr(error))
        raise
    finally:
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
