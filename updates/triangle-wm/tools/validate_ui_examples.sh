#!/usr/bin/env bash
set -euo pipefail
out=${1:-/var/tmp/triangle-ui-examples-release}
native=updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=variants/l1-16k-dma3-lru-ram/software/bosio/include
if [[ ${2:-} != apps-only ]];then bash updates/triangle-wm/tools/validate_solid_resize.sh "$out";fi
g++ -DFIXED_TRIG_TABLES -std=c++17 -O1 -g -I"$native" -I"$uapi" -I"$out" -DMARKER_ENGINE_HEADER="\"$out/src/triangle_wm.cpp\"" updates/triangle-wm/tests/check_large_controls.cpp "$out/src/fixed_reference.cpp" -o "$out/check-large-controls"
"$out/check-large-controls"
g++ -std=c++17 -O2 -pthread -I"$native" -I"$uapi" -Iupdates/triangle-wm/src updates/triangle-wm/src/example_apps.cpp -o "$out/triangle-examples-host"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$native" updates/triangle-wm/tests/check_example_models.cpp -o "$out/check-example-models"
folder=$(mktemp -d)
trap 'rm -f -- "$folder/note.txt"; rmdir -- "$folder"' EXIT
"$out/check-example-models" "$folder"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I"$native" updates/triangle-wm/tests/check_desktop_apps.cpp -o "$out/check-desktop-models"
"$out/check-desktop-models" updates/triangle-wm/config/apps.json "$folder"
for mode in binary json;do TRIANGLE_WIRE=$mode python3 updates/triangle-wm/tests/test_app_events.py "$out/triangle-boayo";done
python3 updates/triangle-wm/tests/test_example_apps.py "$out/triangle-boayo" "$out/triangle-examples-host"
python3 updates/triangle-wm/tests/test_desktop_apps.py "$out/triangle-boayo" "$out/triangle-examples-host" "$(pwd)/updates/triangle-wm/config/apps.json"
echo UI_EXAMPLES_VALIDATION_PASS
