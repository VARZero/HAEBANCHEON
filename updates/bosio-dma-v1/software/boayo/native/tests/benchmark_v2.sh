#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-/var/tmp/oryukdo-boayo-native-v2}
baseline=/var/tmp/oryukdo-boayo-native-final/source-rv32-odfx8M/software/boayo/native
compiler=/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++
mkdir -p "$out/reports/benchmark"
for version in v1 v2; do
 source=$baseline
 extra=()
 if [[ "$version" == v2 ]]; then source=$here; extra=(-DBOAYO_V2); fi
 "$compiler" -std=c++17 -O2 -ffp-contract=off -static -pthread "${extra[@]}" -I"$source/src" \
  "$here/tests/benchmark_v2.cpp" "$source/src/geometry.cpp" "$source/src/backend.cpp" \
  "$source/src/wm.cpp" "$source/reference/wm/native/bosio_compositor.cpp" -o "$out/benchmark-$version"
 "$out/qemu-user/usr/bin/qemu-riscv32" "$out/benchmark-$version" > "$out/reports/benchmark/$version.json"
done
python3 - "$out" <<'PY'
from pathlib import Path
import sys,json
p=Path(sys.argv[1])/'reports/benchmark'
a=json.loads((p/'v1.json').read_text());b=json.loads((p/'v2.json').read_text())
assert a['descriptor_checksum']==b['descriptor_checksum']
assert a['final_shadow_hash']==b['final_shadow_hash']
result=dict(result='PASS',baseline=a,optimized=b,
 reduction_percent={k:100*(1-b[k]/a[k]) for k in ('initial_upload_bytes','geometry_ms','descriptor_batch_ms','one_tile_patch_ms')},
 hardware=False,scope='QEMU RV32 user emulator; not real board latency')
(p/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
PY
