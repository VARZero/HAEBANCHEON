#!/usr/bin/env python3
"""Prepare private, checksum-verified Buildroot/Linux source trees."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BOARD = REPO / 'variants/l1-16k-dma3-lru-ram'
LOCK = json.loads((HERE / 'sources.json').read_text())


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare_buildroot(out):
    item = LOCK['buildroot']
    archive = out / 'downloads' / ('buildroot-' + item['version'] + '.tar.xz')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        partial = archive.with_suffix('.partial')
        urllib.request.urlretrieve(item['url'], partial)
        if sha(partial) != item['sha256']:
            raise ValueError('Buildroot download checksum mismatch')
        partial.rename(archive)
    if sha(archive) != item['sha256']:
        raise ValueError('Buildroot archive checksum mismatch')
    source = out / ('buildroot-' + item['version'])
    if not source.exists():
        # Buildroot skeletons intentionally contain absolute target symlinks.
        # Only this pinned, verified upstream archive uses the tar filter.
        with tempfile.TemporaryDirectory(dir=out, prefix='extract-') as temp:
            with tarfile.open(archive) as tar:
                tar.extractall(temp, filter='tar')
            (Path(temp) / source.name).rename(source)
    return source


def prepare_linux(out):
    source = out / 'sources/linux-6.12'
    if not source.exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=out, prefix='linux-') as temp:
            subprocess.run(['python3', str(REPO / 'scripts/prepare_linux.py'),
                            '--archive', str(out / 'downloads/linux-6.12.tar.xz'),
                            '--output', temp], check=True)
            (Path(temp) / 'linux-6.12').rename(source)
    # Only touch our independent tree. Reject links into other builds.
    for p in source.rglob('*'):
        if p.is_symlink() and not p.resolve().is_relative_to(source):
            raise ValueError(f'External source symlink: {p}')
    storage = BOARD / 'software/storage'
    for name, directory in [('oryukdo_sd.c', 'mmc/host'), ('oryukdo_dma.c', 'dma')]:
        shutil.copyfile(storage / name, source / 'drivers' / directory / name)
    additions = {
        'drivers/mmc/host/Makefile': '\nobj-$(CONFIG_MMC_ORYUKDO) += oryukdo_sd.o\n',
        'drivers/mmc/host/Kconfig': '\nconfig MMC_ORYUKDO\n\tbool "ORYUKDO native SD host"\n\tdepends on MMC && RISCV\n',
        'drivers/dma/Makefile': '\nobj-$(CONFIG_ORYUKDO_DMA) += oryukdo_dma.o\n',
        'drivers/dma/Kconfig': '\nconfig ORYUKDO_DMA\n\tbool "ORYUKDO copy/fill DMA"\n\tdepends on RISCV\n\tselect DMA_ENGINE\n\tselect DMA_VIRTUAL_CHANNELS\n',
        'drivers/misc/Makefile': '\nobj-y += oryukdo-bosio/\n',
    }
    for name, addition in additions.items():
        path = source / name
        text = path.read_text()
        if addition.strip() not in text:
            path.write_text(text + addition)
    dest = source / 'drivers/misc/oryukdo-bosio'
    for directory in ['driver', 'common', 'include']:
        shutil.copytree(BOARD / 'software/bosio' / directory, dest / directory,
                        dirs_exist_ok=True)
    (dest / 'Makefile').write_text('obj-y += driver/\n')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('component', choices=['buildroot', 'linux'])
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    print(prepare_buildroot(out) if args.component == 'buildroot' else prepare_linux(out))


if __name__ == '__main__':
    main()
