#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-/var/tmp/triangle-fixed-release}
cd "$repo"
native=updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=variants/l1-16k-dma3-lru-ram/software/bosio/include
cross=/var/tmp/haebancheon-userland-validation/buildroot/host/bin/riscv32-buildroot-linux-musl-
mkdir -p "$out"
# All geometric constants are generated on the host; no floating startup geometry.
g++ -std=c++17 -O2 -I"$native" updates/triangle-wm/src/generate_geometry.cpp "$native/geometry.cpp" -o "$out/generate"
"$out/generate" > "$out/tile_geometry.hpp"
python3 updates/triangle-wm/tools/generate_neighbors.py "$native/fixed_geometry.inc" "$out/tile_neighbors.hpp"
g++ -std=c++17 -O2 -I"$native" updates/triangle-wm/tests/generate_primitive_init.cpp "$native/geometry.cpp" -o "$out/generate-init"
"$out/generate-init" > "$out/primitive_init.h"
g++ -std=c++17 -O2 -I"$native" -Iupdates/triangle-wm/src updates/triangle-wm/tests/generate_fixed_trig.cpp -o "$out/generate-trig"
"$out/generate-trig" > "$out/fixed_trig_table.hpp"
python3 updates/triangle-wm/tools/prepare_fixed.py "$out/src"
flags=(-DFIXED_TRIG_TABLES -std=c++17 -O2 -g -pthread -ffp-contract=off -static -march=rv32ima_zicsr_zifencei -mabi=ilp32)
includes=(-I"$native" -I"$uapi" -I"$out" -I"$out/src")
"${cross}g++" "${flags[@]}" "${includes[@]}" "$out/src/boayo_service.cpp" "$out/src/fixed_reference.cpp" -o "$out/triangle-boayo-fixed-debug"
"${cross}strip" -o "$out/triangle-boayo-fixed" "$out/triangle-boayo-fixed-debug"
"${cross}g++" "${flags[@]}" -s "${includes[@]}" "$out/src/boayo_app.cpp" -o "$out/triangle-app"
"${cross}g++" "${flags[@]}" -s "${includes[@]}" "$out/src/input_client.cpp" -o "$out/triangle-input"
"${cross}g++" "${flags[@]}" "${includes[@]}" -c updates/triangle-wm/tests/fixed_core_probe.cpp -o "$out/fixed-core.o"
"${cross}nm" -u "$out/fixed-core.o" > "$out/fixed-core-symbols.txt"
if grep -E '(__.*[ds]f[23]|sin|cos|atan|sqrt|pow)' "$out/fixed-core-symbols.txt"; then echo FLOAT_CORE_AUDIT_FAILED;exit 1;fi
sha256sum "$out/triangle-boayo-fixed" "$out/triangle-app" "$out/triangle-input" > "$out/SHA256SUMS"
python3 - "$out" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);names=['triangle-boayo-fixed','triangle-app','triangle-input']
data=dict(numeric='fixed-q24',wire='BWM1',preview='full-move-solid-resize',move_marker_cells=1024,fast_motion='PTR1-one-way-drain-bounded',demo_diagnostics='startup-only',resize_preview='solid-filled-direct-cached-trig-q13-coarse',window_colors='RGB332-256-independent-SB',runtime_priority={'wm':-10,'input':-5,'app':10},binaries={n:hashlib.sha256((p/n).read_bytes()).hexdigest() for n in names})
(p/'features.json').write_text(json.dumps(data,indent=2)+'\n')
PY
echo FIXED_BUILD_AND_INTEGER_CORE_AUDIT_PASS
