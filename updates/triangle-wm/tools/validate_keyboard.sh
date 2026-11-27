#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo"
out=${1:-/var/tmp/triangle-keyboard-v16/host}
reports=/var/tmp/triangle-keyboard-v16/validation
mkdir -p "$out" "$reports"
for name in tile_geometry.hpp tile_neighbors.hpp fixed_trig_table.hpp primitive_init.h;do cp "/var/tmp/triangle-keyboard-v16/userland/$name" "$out/$name";done
native=updates/boayo-tilebatch-v14/software/boayo/native/src
python3 updates/triangle-wm/tools/prepare_fixed.py "$out/src"
g++ -std=c++17 -O1 -pthread -DFIXED_TRIG_TABLES -I"$native" -Ivariants/l1-16k-dma3-lru-ram/software/bosio/include -I"$out" -I"$out/src" "$out/src/boayo_service.cpp" "$out/src/fixed_reference.cpp" -o "$out/triangle-boayo"
g++ -std=c++17 -O1 -pthread -I"$native" updates/triangle-wm/src/example_apps.cpp -o "$out/triangle-examples-host"
g++ -std=c++17 -O1 -pthread -I"$native" updates/triangle-wm/src/boayo_input.cpp -o "$out/boayo-input-host"
g++ -std=c++17 -O1 -Iupdates/triangle-wm/src updates/triangle-wm/tests/check_key_transport.cpp -o "$out/check-key-transport"
"$out/check-key-transport"
g++ -std=c++17 -O1 -DFIXED_TRIG_TABLES -I"$native" -Ivariants/l1-16k-dma3-lru-ram/software/bosio/include -I"$out" -DMARKER_ENGINE_HEADER="\"$out/src/triangle_wm.cpp\"" updates/triangle-wm/tests/check_large_controls.cpp "$out/src/fixed_reference.cpp" -o "$out/check-large-controls"
"$out/check-large-controls"
python3 updates/triangle-wm/tests/test_keyboard_navigation.py "$out/triangle-boayo" "$out/triangle-examples-host" "$out/boayo-input-host" | tee "$reports/tty.log"
gcc -shared -fPIC -O1 updates/triangle-wm/tests/spi_keyboard_fixture.c -ldl -o "$out/spi-keyboard-fixture.so"
python3 updates/triangle-wm/tests/test_spi_keyboard.py "$out/triangle-boayo" "$out/boayo-input-host" "$out/spi-keyboard-fixture.so" | tee "$reports/spi.log"
for mode in binary json;do TRIANGLE_WIRE=$mode python3 updates/triangle-wm/tests/test_app_events.py "$out/triangle-boayo";done
python3 updates/triangle-wm/tests/test_example_apps.py "$out/triangle-boayo" "$out/triangle-examples-host"
python3 updates/triangle-wm/tests/test_desktop_apps.py "$out/triangle-boayo" "$out/triangle-examples-host" "$repo/updates/triangle-wm/config/apps.json"
echo KEYBOARD_NATIVE_VALIDATION_PASS
