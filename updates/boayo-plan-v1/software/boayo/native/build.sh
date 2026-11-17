#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-/var/tmp/oryukdo-boayo-native}
mode=${2:-host}
python3 "$here/plan_revision.py"
mkdir -p "$out"
snapshot=$(mktemp -d "$out/source-$mode-XXXXXX")
source="$snapshot/software/boayo/native"
mkdir -p "$source/reference/wm/native" "$snapshot/software/bosio/include"
cp -a "$here/src" "$here/vendor" "$source/"
cp "$here/reference/wm/native/bosio_compositor.cpp" "$source/reference/wm/native/"
cp "$here/../../bosio/include/bosio_uapi.h" "$snapshot/software/bosio/include/"
echo "BUILD_SOURCE $source"
compiler=g++
flags=(-std=c++17 -O2 -g -ffp-contract=off -pthread)
if [[ "$mode" == rv32 ]]; then
 compiler=/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++
 flags+=(-static -march=rv32ima_zicsr_zifencei -mabi=ilp32 -ffunction-sections -fdata-sections -Wl,--gc-sections)
fi
if [[ "$mode" == sanitize ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$compiler" "${flags[@]}" "$source/src/plan.cpp" "$source/src/assets.cpp" "$source/src/geometry.cpp" "$source/src/wm.cpp" "$source/src/ui.cpp" "$source/src/backend.cpp" "$source/src/apps.cpp" "$source/src/runtime.cpp" "$source/src/selftest.cpp" "$source/src/main.cpp" "$source/reference/wm/native/bosio_compositor.cpp" -o "$out/boayo-native-$mode"
python3 "$here/build_manifest.py" "$source" "$out/boayo-native-$mode"
echo "BUILD_OK $out/boayo-native-$mode"
