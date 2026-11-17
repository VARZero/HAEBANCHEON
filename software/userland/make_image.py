#!/usr/bin/env python3
"""Append the development rootfs to the unchanged ORYSD001 boot layout.

Only regular image files are written. No physical disks are opened.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BOARD = REPO / 'variants/l1-16k-dma3-lru-ram'
HEADER = 2048 * 512
ADDRESSES = (0x80000000, 0x80400000, 0x87f00000)
LIMITS = (8192, 251904, 2048)


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def inspect_image(path):
    """Audit partition bounds, load ranges, payload CRCs and ext4 superblock."""
    with Path(path).open('rb') as f:
        mbr = f.read(512)
        if mbr[510:512] != b'\x55\xaa':
            raise ValueError('Invalid MBR signature')
        p1, p2 = [struct.unpack_from('<B3sB3sII', mbr, offset)
                  for offset in (446, 462)]
        if p1[2] != 0xda or p1[4] != 2048 or p2[2] != 0x83:
            raise ValueError('Invalid boot/root partition types or boot LBA')
        if p1[4] + p1[5] > p2[4] or p2[4] % 2048:
            raise ValueError('Overlapping or unaligned root partition')
        if (p2[4] + p2[5]) * 512 != Path(path).stat().st_size:
            raise ValueError('Partition extends beyond image or trailing data')
        f.seek(HEADER)
        header = f.read(512)
        if header[:8] != b'ORYSD001':
            raise ValueError('Invalid loader header')
        segments = []
        sectors = 1
        for index, limit in enumerate(LIMITS):
            count, expected = struct.unpack_from('<II', header, 8 + index * 8)
            if not 0 < count <= limit:
                raise ValueError('Payload exceeds bootloader load range')
            payload = f.read(count * 512)
            if len(payload) != count * 512 or zlib.crc32(payload) != expected:
                raise ValueError('Payload CRC mismatch')
            segments.append({'address': hex(ADDRESSES[index]), 'sectors': count,
                             'crc32': f'{expected:08x}'})
            sectors += count
        if sectors != p1[5]:
            raise ValueError('Boot partition size does not match payloads')
        f.seek(p2[4] * 512 + 1024)
        superblock = f.read(1024)
        if superblock[56:58] != b'\x53\xef':
            raise ValueError('Root partition is not ext4')
        blocks = struct.unpack_from('<I', superblock, 4)[0]
        block_size = 1024 << struct.unpack_from('<I', superblock, 24)[0]
        if blocks * block_size != p2[5] * 512:
            raise ValueError('Filesystem/partition size mismatch')
    return {'boot_lba': p1[4], 'root_lba': p2[4], 'root_sectors': p2[5],
            'segments': segments}


def assemble(boot, rootfs, target):
    size = rootfs.stat().st_size
    if size == 0 or size % 512:
        raise ValueError('Rootfs must be non-empty and sector aligned')
    first = (boot.stat().st_size + 1048575) // 1048576 * 2048
    with target.open('wb') as dst, boot.open('rb') as src:
        shutil.copyfileobj(src, dst)
        dst.seek(first * 512)
        with rootfs.open('rb') as src:
            shutil.copyfileobj(src, dst)
        dst.seek(462)
        dst.write(struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 0x83,
                              b'\xfe\xff\xff', first, size // 512))
    return inspect_image(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    fw = out / 'firmware'
    lock = json.loads((HERE / 'sources.json').read_text())
    if sha(fw / 'fw_jump.bin') != lock['opensbi']['sha256']:
        raise ValueError('Published OpenSBI was changed')
    if (fw / 'gen3.dts').read_bytes() != (BOARD / 'software/bosio/gen3-bosio.dts').read_bytes():
        raise ValueError('Board device tree was changed')
    images = out / 'images'
    images.mkdir(exist_ok=True)
    subprocess.run(['python3', str(BOARD / 'soc/board/make_sd.py'),
                    str(images / 'boot'), '--firmware', str(fw)], check=True)
    boot = images / 'boot/ORYUKDO-Gen3-BOSIO-KFB-1GiB-BusyBox.img'
    subprocess.run(['python3', str(BOARD / 'soc/board/verify_image.py'), str(boot),
                    str(fw), str(images / 'boot/gen3.dtb')], check=True)
    rootfs = out / 'buildroot/images/rootfs.ext4'
    subprocess.run(['e2fsck', '-fn', str(rootfs)], check=True)
    target = images / 'HAEBANCHEON-userland.img'
    manifest = assemble(boot, rootfs, target)
    manifest.update(image=target.name, bytes=target.stat().st_size,
                    sha256=sha(target), rootfs_sha256=sha(rootfs),
                    opensbi_sha256=sha(fw / 'fw_jump.bin'),
                    kernel_sha256=sha(fw / 'Image'), hardware_tested=False,
                    requires='Current L1-16K DMA3 LRU-RAM bitstream; unchanged RTL',
                    source_date_epoch=lock['source_date_epoch'])
    inputs = list(HERE.rglob('*')) + list((BOARD / 'software/bosio').rglob('*'))
    inputs += [BOARD / 'software/storage' / name for name in
               ['linux.config', 'oryukdo_sd.c', 'oryukdo_dma.c']]
    inputs += list((REPO / 'software/linux').rglob('*'))
    manifest['sources'] = {p.relative_to(REPO).as_posix(): sha(p)
                           for p in sorted(inputs) if p.is_file()
                           and '__pycache__' not in p.parts and p.suffix != '.pyc'}
    apps = out / 'buildroot/target/data/apps'
    manifest['apps'] = {p.relative_to(apps).as_posix(): sha(p)
                        for p in sorted(apps.rglob('*')) if p.is_file() and not p.is_symlink()}
    for source, name in [(out / 'buildroot/.config', 'buildroot.config'),
                         (out / 'kernel/.config', 'linux.config'),
                         (out / 'bootstrap-build/.config', 'bootstrap.config')]:
        shutil.copyfile(source, images / name)
        manifest[name + '_sha256'] = sha(source)
    manifest['bootstrap_busybox_sha256'] = sha(out / 'bootstrap-build/busybox')
    (images / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k: v for k, v in manifest.items() if k != 'sources'}, indent=2))


if __name__ == '__main__':
    main()
