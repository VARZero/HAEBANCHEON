#!/usr/bin/env python3
"""Reject RV64, compressed-instruction or hardware-float ABI target objects."""
import argparse
import json
from pathlib import Path
import struct


def check(root):
    files = []
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as f:
            head = f.read(52)
        if head[:4] != b'\x7fELF':
            continue
        if len(head) < 52 or head[4:6] != b'\x01\x01' or struct.unpack_from('<H', head, 18)[0] != 243:
            raise ValueError(f'Not little-endian RV32 ELF: {path}')
        flags = struct.unpack_from('<I', head, 36)[0]
        if flags & 0x1f:
            raise ValueError(f'Unsupported RVC/float/RVE/TSO ELF flags {flags:#x}: {path}')
        files.append(path.relative_to(root).as_posix())
    if not files:
        raise ValueError('No ELF files found')
    return {'result': 'PASS_ELF_HEADERS', 'files': files,
            'limitation': 'Header/ABI audit only; QEMU with C/F/D/V disabled tests executed instructions.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = check(args.root)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'PASS: {len(result["files"])} RV32 soft-float ELF headers')
