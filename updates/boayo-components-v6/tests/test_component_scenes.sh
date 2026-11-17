#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
repo=$(cd "$here/../../.."; pwd)
work=${1:-/var/tmp/haebancheon-components-v6}
rtl="$repo/variants/l1-16k-dma3-lru-ram/soc/vendor/output"
mkdir -p "$work/reports" "$work/component-rtl"
verilator --binary --timing --assert -Wno-fatal --top-module tb_component_scenes -j 4 --Mdir "$work/component-rtl" "$here/tb_component_scenes.sv" "$rtl/bosio_ddr_scene_cache.v" "$rtl/varzero_cache_line.v" > "$work/reports/component-rtl-build.log" 2>&1
"$work/component-rtl/Vtb_component_scenes" +root="$work/reports/components" > "$work/reports/component-rtl.log" 2>&1
cat "$work/reports/component-rtl.log"
