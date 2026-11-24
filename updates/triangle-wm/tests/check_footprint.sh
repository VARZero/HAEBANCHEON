#!/usr/bin/env bash
set -euo pipefail
out=$1
native=updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=variants/l1-16k-dma3-lru-ram/software/bosio/include
g++ -DFIXED_TRIG_TABLES -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$native" -I"$uapi" -I"$out" -DMARKER_ENGINE_HEADER="\"$out/src/triangle_wm.cpp\"" updates/triangle-wm/tests/check_footprint.cpp "$out/src/fixed_reference.cpp" -o "$out/check-footprint"
"$out/check-footprint"
