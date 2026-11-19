#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$here/../../.." && pwd)
out=${1:-/var/tmp/haebancheon-bosio-palette-tag}
mkdir -p "$out"
rtl="$repo/variants/l1-16k-dma3-lru-ram/soc/vendor/output"
verilator --cc --exe --build --assert --no-timing --unroll-count 512 -j 3 -O3 -Wno-fatal \
 --top-module bosio_ddr_scene_cache --Mdir "$out/model" -CFLAGS '-O2 -std=c++17' \
 "$rtl/bosio_ddr_scene_cache.v" "$rtl/varzero_cache_line.v" "$here/bosio_palette_tag_rtl.cpp" \
 > "$out/build.log" 2>&1
"$out/model/Vbosio_ddr_scene_cache" | tee "$out/result.log"
