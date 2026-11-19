#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.."; pwd)
out=${1:-/var/tmp/oryukdo-bosio-driver-rtl}
mode=${2:-}
case "$mode" in
 ''|--scratch) ;;
 *) echo "usage: run_rtl.sh [output] [--scratch]" >&2; exit 2;;
esac
mkdir -p "$out"
if [[ -z "$mode" ]]; then
 mkdir -p "$root/software/bosio/reports"
fi
cd "$root"
python3 - "$out/sources.f" <<'PY'
from pathlib import Path
import sys
names=['soc/bosio/or_bo_axi_pkg.sv','soc/bosio/or_bo_axi_fabric.sv','soc/bosio/or_bo_bosio_axi.sv','soc/rtl/gen3_mul33_pipe.sv']
names += ['soc/bosio/gen3_axil_regs.sv', 'soc/bosio/or_storage_plic.sv']
names += [n for n in Path('soc/bosio_sources.f').read_text().splitlines() if n.startswith('soc/vendor/')]
names += ['software/bosio/tests/bo_driver_ram.sv','software/bosio/tests/tb_bosio_driver.sv']
Path(sys.argv[1]).write_text('\n'.join(names)+'\n')
PY
gcc -std=c11 -Wall -Wextra -Werror -O2 -c software/bosio/common/bosio_engine.c -o "$out/engine.o"
verilator --cc --exe --build --assert --no-timing --unroll-count 512 -j 2 -O3 -Wno-fatal --top-module tb_bosio_driver \
  --Mdir "$out/model" -CFLAGS '-O2 -std=c++17' -LDFLAGS "$out/engine.o" \
  -f "$out/sources.f" "$root/software/bosio/tests/test_rtl.cpp" >"$out/build.log" 2>&1
for seed in 1 7 23; do
 if [[ "$mode" == --scratch ]]; then
  "$out/model/Vtb_bosio_driver" "$seed" --scratch +seed="$seed" | tee "$out/scratch_seed${seed}.log"
 else
  "$out/model/Vtb_bosio_driver" "$seed" +seed="$seed" | tee "$root/software/bosio/reports/rtl_seed${seed}.log"
 fi
done
