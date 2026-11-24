#!/usr/bin/env bash
set -euo pipefail
out=${1:-/var/tmp/triangle-marker-host}
native=updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=variants/l1-16k-dma3-lru-ram/software/bosio/include
flags=(-DFIXED_TRIG_TABLES -std=c++17 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-omit-frame-pointer)
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
mkdir -p updates/triangle-wm/evidence/single-tile
g++ "${flags[@]}" -I"$native" -I"$uapi" -I"$out" -DMARKER_ENGINE_HEADER="\"$out/src/triangle_wm.cpp\"" updates/triangle-wm/tests/check_move_marker.cpp "$out/src/fixed_reference.cpp" -o "$out/check-marker"
"$out/check-marker"
g++ "${flags[@]}" -I"$native" -I"$uapi" -I"$out" updates/triangle-wm/tests/check_feedback.cpp "$native/geometry.cpp" -o "$out/check-feedback"
"$out/check-feedback"
g++ "${flags[@]}" -I"$native" -I"$uapi" -I"$out" updates/triangle-wm/tests/check_ui_delta.cpp "$native/geometry.cpp" -o "$out/check-ui-delta"
"$out/check-ui-delta"
for wire in binary json; do
 export TRIANGLE_WIRE=$wire
 TEST_INPUT_VISIBILITY=1 python3 updates/triangle-wm/tests/test_input.py "$out/triangle-boayo"
 python3 updates/triangle-wm/tests/test_input_mailbox.py "$out/triangle-boayo"
 python3 updates/triangle-wm/tests/test_move_marker.py "$out/triangle-boayo"
done
export TRIANGLE_WIRE=binary
TRIANGLE_INPUT=0 python3 updates/triangle-wm/tests/test_boayo.py "$out/triangle-boayo"
python3 updates/triangle-wm/tests/test_coalescing.py "$out/triangle-boayo"
TRIANGLE_PREVIEW=outline python3 updates/triangle-wm/tests/test_coalescing.py "$out/triangle-boayo"
TRIANGLE_PREVIEW=outline TEST_INPUT_VISIBILITY=1 python3 updates/triangle-wm/tests/test_input.py "$out/triangle-boayo"
echo MARKER_VALIDATION_PASS
