#!/usr/bin/env bash
set -euo pipefail
ulimit -s unlimited
cd "$(dirname "$0")/../.."
mkdir -p validation
for backend in axi native;do
 flags=();build=/var/tmp/haebancheon-l1-16k-lru-cpu-test
 if [[ "$backend" == native ]];then flags=(-DKFB_NATIVE_BACKEND);build=/var/tmp/haebancheon-l1-16k-lru-cpu-native-test;fi
 test -x "$build/Vtb_cpu_storage" || { echo 'Run test_cpu.sh with both backends first';exit 1; }
 prefix="validation/cpu_sv32_$backend"
 riscv64-unknown-elf-gcc "${flags[@]}" -march=rv32ima_zicsr_zifencei -mabi=ilp32 -O2 -ffreestanding -fno-builtin -fno-pic -msmall-data-limit=0 -mno-relax -nostdlib -nostartfiles -Wl,--no-relax -T soc/firmware/link.ld soc/firmware/start.S soc/tests/cpu_cache_sv32.c -o "$prefix.elf"
 riscv64-unknown-elf-objcopy -O binary "$prefix.elf" "$prefix.bin"
 python3 - "$prefix" <<'PY'
from pathlib import Path
import sys
p=sys.argv[1];b=Path(p+'.bin').read_bytes();b+=b'\0'*(-len(b)%4)
Path(p+'.hex').write_text(''.join(f'{int.from_bytes(b[i:i+4],"little"):08x}\n' for i in range(0,len(b),4)))
PY
 for seed in 1 7 23;do
  log="${prefix}_seed${seed}.log"
  "$build/Vtb_cpu_storage" +image="$prefix.hex" +seed="$seed" > "$log" 2>&1
  echo 'PASS CPU_SV32_CACHE virtual aliases, page-table update/SFENCE.VMA, coherent DMA1/DMA2, S-mode IRQ with paging' | tee -a "$log"
 done
done
