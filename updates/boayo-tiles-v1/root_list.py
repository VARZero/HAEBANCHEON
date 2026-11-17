"""Generate deterministic gen_init_cpio input, preserving original init and applets."""
import os
from pathlib import Path
import stat
import sys
root=Path(sys.argv[1]).resolve()
def visit(directory):
    for entry in sorted(directory.iterdir()):
        target='/'+entry.relative_to(root).as_posix()
        mode=stat.S_IMODE(entry.lstat().st_mode)
        if entry.is_symlink():
            print(f'slink {target} {os.readlink(entry)} {mode:04o} 0 0')
        elif entry.is_dir():
            print(f'dir {target} {mode:04o} 0 0');visit(entry)
        elif entry.is_file():
            if ' ' in str(entry): raise ValueError('Whitespace in initramfs path')
            print(f'file {target} {entry} {mode:04o} 0 0')
        else: raise ValueError(f'Unexpected special file {entry}')
visit(root)
print('nod /dev/console 0600 0 0 c 5 1')
