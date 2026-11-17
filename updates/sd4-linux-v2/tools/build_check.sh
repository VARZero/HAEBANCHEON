#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
out=${1:?output ELF required}
riscv64-linux-gnu-gcc -march=rv32ima -mabi=ilp32 -Os -nostdlib -static \
 -fno-pie -no-pie -fno-stack-protector -fno-builtin -fno-tree-loop-distribute-patterns -mno-relax \
 -Wall -Wextra -Werror -Wno-misleading-indentation \
 -Wl,--build-id=none,-e,_start,-s "$here/storage_check.c" -o "$out"
file "$out"
