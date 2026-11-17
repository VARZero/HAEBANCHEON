#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")/..";pwd)
repo=$(cd "$here/../..";pwd)
work=${1:-/var/tmp/oryukdo-boayo-tiles-v4}
rtl="$repo/variants/sd4-dma/soc/vendor/output"
sha256sum "$rtl/bosio_ddr_scene_cache.v" "$rtl/varzero_cache_line.v" > "$work/reports/scene-rtl-sources.txt"
verilator --binary --timing --assert -Wno-fatal --top-module tb_scene_switch -j 4 --Mdir "$work/scene-rtl" "$here/tests/tb_scene_switch.sv" "$rtl/bosio_ddr_scene_cache.v" "$rtl/varzero_cache_line.v" > "$work/reports/scene-rtl-build.log" 2>&1
"$work/scene-rtl/Vtb_scene_switch" > "$work/reports/scene-rtl.log" 2>&1
