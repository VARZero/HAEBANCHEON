#!/usr/bin/env bash
set -euo pipefail
out=${1:-/var/tmp/triangle-solid-resize-release}
native=updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=variants/l1-16k-dma3-lru-ram/software/bosio/include
g++ -DFIXED_TRIG_TABLES -std=c++17 -O2 -g -pthread -I"$native" -I"$uapi" -I"$out" -I"$out/src" "$out/src/boayo_service.cpp" "$out/src/fixed_reference.cpp" -o "$out/triangle-boayo"
g++ -DFIXED_TRIG_TABLES -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$native" -I"$uapi" -I"$out" -DMARKER_ENGINE_HEADER="\"$out/src/triangle_wm.cpp\"" updates/triangle-wm/tests/check_move_marker.cpp "$out/src/fixed_reference.cpp" -o "$out/check-marker"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$out/check-marker"
for mode in binary json; do
 export TRIANGLE_WIRE=$mode
 python3 updates/triangle-wm/tests/test_move_marker.py "$out/triangle-boayo"
 python3 updates/triangle-wm/tests/test_solid_resize.py "$out/triangle-boayo"
 TEST_INPUT_VISIBILITY=1 python3 updates/triangle-wm/tests/test_input.py "$out/triangle-boayo"
 python3 updates/triangle-wm/tests/test_input_mailbox.py "$out/triangle-boayo"
done
export TRIANGLE_WIRE=binary
TRIANGLE_INPUT=0 python3 updates/triangle-wm/tests/test_boayo.py "$out/triangle-boayo"
python3 updates/triangle-wm/tests/test_coalescing.py "$out/triangle-boayo"
g++ -DFIXED_TRIG_TABLES -std=c++17 -O2 -pthread -I"$native" -I"$uapi" -I"$out" -I"$out/src" "$out/src/input_client.cpp" -o "$out/triangle-input-host"
python3 updates/triangle-wm/tests/test_fast_client.py "$out/triangle-boayo" "$out/triangle-input-host"
echo SOLID_RESIZE_VALIDATION_PASS
