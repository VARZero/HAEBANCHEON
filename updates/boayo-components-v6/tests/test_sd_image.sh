#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.."; pwd)
repo=$(cd "$here/../.."; pwd)
work=${1:-/var/tmp/haebancheon-components-v6}
cd "$repo/variants/l1-16k-dma3-lru-ram"
files=(soc/bosio/or_bo_axi_pkg.sv soc/bosio/gen3_axil_regs.sv
 soc/bosio/or_storage_dma.sv soc/bosio/or_sd_native.sv soc/bosio/or_sd_storage.sv
 soc/board/sd_boot.sv soc/tests/axi_ram_model.sv soc/tests/tb_sd_storage.sv
 soc/tests/test_sd_storage.cpp)
sha256sum "${files[@]}" soc/tests/native_card_model.hpp > "$work/reports/sd-rtl-sources.txt"
# Only the simulation watchdog scales with the exact image payload. The old
# 200M-cycle bound was sized for a much smaller initramfs; hardware is unchanged.
python3 - "$PWD/${files[8]}" "$work/test_sd_storage.cpp" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
s=s.replace('unsigned pending_addr=0,cycles=0;', 'unsigned pending_addr=0;uint64_t cycles=0;')
s=s.replace('auto tick=[&](){', '''uint64_t budget=6000000;
 if(mode=="image"){
  budget=1000000;
  for(int seg=0;seg<3;seg++){
   uint32_t count=0;for(int j=0;j<4;j++)count|=uint32_t(h[8+seg*8+j])<<(8*j);
   budget+=uint64_t(count)*40000;
  }
 }
 auto tick=[&](){''')
s=s.replace('(mode=="image"?200000000u:6000000u)','budget')
Path(sys.argv[2]).write_text(s)
PY
verilator --cc --exe --build -j 4 -Wno-TIMESCALEMOD --assert --top-module tb_sd_storage \
 -CFLAGS "-I$PWD/soc/tests" --Mdir "$work/sd-rtl" "${files[@]:0:8}" "$work/test_sd_storage.cpp" > "$work/reports/sd-rtl-build.log" 2>&1
"$work/sd-rtl/Vtb_sd_storage" image "$work/release/image/HAEBANCHEON-KFB-BoAYO-components-v6.img" > "$work/reports/sd-rtl.log" 2>&1
