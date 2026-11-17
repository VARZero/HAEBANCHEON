#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")/.."; pwd)
repo=$(cd "$here/../.."; pwd)
work=${1:-/var/tmp/oryukdo-boayo-tiles-v2}
cd "$repo/variants/sd4-dma"
files=(soc/bosio/or_bo_axi_pkg.sv soc/bosio/gen3_axil_regs.sv
 soc/bosio/or_storage_dma.sv soc/bosio/or_sd_native.sv soc/bosio/or_sd_storage.sv
 soc/board/sd_boot.sv soc/tests/axi_ram_model.sv soc/tests/tb_sd_storage.sv
 soc/tests/test_sd_storage.cpp)
sha256sum "${files[@]}" soc/tests/native_card_model.hpp > "$work/reports/sd-rtl-sources.txt"
verilator --cc --exe --build -j 4 -Wno-TIMESCALEMOD --assert --top-module tb_sd_storage \
 --Mdir "$work/sd-rtl" "${files[@]:0:8}" "$PWD/${files[8]}" > "$work/reports/sd-rtl-build.log" 2>&1
"$work/sd-rtl/Vtb_sd_storage" image "$work/release/image/ORYUKDO-KFB-BoAYO-tiles-v2.img" > "$work/reports/sd-rtl.log" 2>&1
