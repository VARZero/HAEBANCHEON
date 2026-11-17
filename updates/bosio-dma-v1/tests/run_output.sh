#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
native="$here/../software/boayo/native"
out=${1:-/var/tmp/oryukdo-bosio-dma-output}
mkdir -p "$out"
g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections \
 -fsanitize=address,undefined -fno-omit-frame-pointer -ffp-contract=off \
 "$here/test_output_dma.cpp" "$native/src/backend.cpp" "$native/src/geometry.cpp" \
 "$native/src/wm.cpp" "$native/reference/wm/native/bosio_compositor.cpp" \
 -o "$out/test_output_dma"
"$out/test_output_dma"
