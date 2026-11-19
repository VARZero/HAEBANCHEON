#!/usr/bin/env python3
"""Test the new RV32 WM against the existing image in a disposable QEMU copy."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from qemu_smoke import Console, image, qemu_dts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    layout = image.inspect_image(args.image)
    args.output.mkdir(parents=True, exist_ok=True)
    regression = Path(__file__).resolve().parents[3] / "updates/boayo-tilebatch-v14/tests/test_wm_daemon.py"
    report = {"scope": "QEMU RV32 Linux, shared memory and mock BOSIO",
              "physical_board_tested": False, "target_ms": 300,
              "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              "checks": []}
    try:
        with tempfile.TemporaryDirectory(prefix="haeb-wm-qemu-") as temporary:
            root = Path(temporary)
            disk = root / "sd.img"
            subprocess.run(["cp", "--reflink=auto", "--sparse=always", str(args.image), str(disk)], check=True)
            rootfs = root / "rootfs.ext4"
            with args.image.open("rb") as source, rootfs.open("wb") as target:
                source.seek(layout["root_lba"]*512)
                remaining = layout["root_sectors"]*512
                while remaining:
                    chunk = source.read(min(8*1024*1024, remaining))
                    if not chunk:
                        raise ValueError("truncated root partition")
                    target.write(chunk)
                    remaining -= len(chunk)
            commands = root / "debugfs.commands"
            commands.write_text(
                "rm /usr/bin/bosio-tiles\n"
                f"write {args.binary.resolve()} /usr/bin/bosio-tiles\n"
                "sif /usr/bin/bosio-tiles mode 0100755\n"
                f"write {regression} /data/test_wm_daemon.py\n"
            )
            with (args.output / "debugfs.log").open("wb") as log:
                subprocess.run(["debugfs", "-w", "-f", str(commands), str(rootfs)],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            with disk.open("r+b") as target, rootfs.open("rb") as source:
                target.seek(layout["root_lba"]*512)
                while chunk := source.read(8*1024*1024):
                    target.write(chunk)
            command = ["qemu-system-riscv32", "-machine", "virt",
                       "-cpu", "rv32,c=false,f=false,d=false,v=false,zfa=false,zawrs=false",
                       "-m", "1G", "-smp", "1", "-bios", "none", "-display", "none",
                       "-monitor", "none", "-serial", "stdio", "-no-reboot",
                       "-drive", f"file={disk},if=none,format=raw,id=sd",
                       "-device", "virtio-blk-device,drive=sd,bus=virtio-mmio-bus.0"]
            with args.image.open("rb") as source:
                source.seek(image.HEADER+512)
                for index, segment in enumerate(layout["segments"]):
                    payload = root / f"payload{index}"
                    payload.write_bytes(source.read(segment["sectors"]*512))
                    if index != 2:
                        command += ["-device", f"loader,file={payload},addr={segment['address']},force-raw=on"]
            dts, dtb = root / "qemu.dts", root / "qemu.dtb"
            dts.write_text(qemu_dts())
            subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", str(dtb), str(dts)], check=True)
            entry = root / "entry"
            entry.write_bytes(struct.pack("<4I", 0x00000513, 0x87f005b7, 0x800002b7, 0x00028067))
            command += ["-device", f"loader,file={dtb},addr=0x87f00000,force-raw=on",
                        "-device", f"loader,file={entry},addr=0x80200000,force-raw=on,cpu-num=0"]
            with (args.output / "uart.log").open("wb") as log:
                console = Console(command, log)
                try:
                    console.until(rb"haebancheon# ")
                    checks = [
                        "python3 /data/test_wm_daemon.py /usr/bin/bosio-tiles",
                        "(set -e; boayo start --mock; trap 'boayo stop' EXIT; "
                        "boayo bench-small 5; boayo status)",
                    ]
                    for check in checks:
                        console.run(check, timeout=900)
                        report["checks"].append(check)
                finally:
                    console.close()
        report["result"] = "PASS_QEMU_WM_MOCK"
    except BaseException as error:
        report.update(result="FAIL", error=repr(error))
        raise
    finally:
        (args.output / "result.json").write_text(json.dumps(report, indent=2)+"\n")


if __name__ == "__main__":
    main()
