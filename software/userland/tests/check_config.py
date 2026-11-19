#!/usr/bin/env python3
"""Fail if Kconfig silently drops a required package, driver or ABI setting."""
import argparse
from pathlib import Path


def check(path, kind):
    values = dict(line.split('=', 1) for line in path.read_text().splitlines()
                  if line.startswith(('CONFIG_', 'BR2_')) and '=' in line)
    if kind == 'buildroot':
        required = ['BR2_RISCV_32', 'BR2_RISCV_ABI_ILP32', 'BR2_RISCV_USE_MMU',
                    'BR2_TOOLCHAIN_BUILDROOT_MUSL', 'BR2_TOOLCHAIN_BUILDROOT_CXX',
                    'BR2_PACKAGE_BUSYBOX', 'BR2_PACKAGE_VIM', 'BR2_PACKAGE_PYTHON3',
                    'BR2_PACKAGE_NATIVE_GCC', 'BR2_PACKAGE_BINUTILS_TARGET',
                    'BR2_PACKAGE_MAKE', 'BR2_TARGET_ROOTFS_EXT2_4']
        forbidden = ['BR2_RISCV_64', 'BR2_RISCV_ISA_RVF', 'BR2_RISCV_ISA_RVD',
                     'BR2_RISCV_ISA_RVC', 'BR2_RISCV_ISA_RVV']
    else:
        required = ['CONFIG_32BIT', 'CONFIG_MMU', 'CONFIG_BINFMT_ELF',
                    'CONFIG_BINFMT_SCRIPT', 'CONFIG_DEVTMPFS', 'CONFIG_EXT4_FS',
                    'CONFIG_MMC_BLOCK', 'CONFIG_MMC_ORYUKDO', 'CONFIG_ORYUKDO_DMA',
                    'CONFIG_SIFIVE_PLIC', 'CONFIG_MSDOS_PARTITION', 'CONFIG_HVC_RISCV_SBI',
                    'CONFIG_VIRTIO_MMIO', 'CONFIG_VIRTIO_BLK', 'CONFIG_TMPFS', 'CONFIG_FILE_LOCKING']
        required += ['CONFIG_SHMEM', 'CONFIG_MEMFD_CREATE']
        forbidden = ['CONFIG_64BIT', 'CONFIG_FPU', 'CONFIG_RISCV_ISA_C']
    wrong = [key for key in required if values.get(key) != 'y']
    wrong += [key for key in forbidden if values.get(key) == 'y']
    if wrong:
        raise ValueError(f'{kind} configuration mismatch: {wrong}')
    print(f'PASS {kind} required configuration')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['buildroot', 'linux'])
    parser.add_argument('config', type=Path)
    args = parser.parse_args()
    check(args.config, args.kind)
