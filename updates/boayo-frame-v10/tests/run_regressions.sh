#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
updates=$(cd "$here/../.." && pwd)
work=${1:-/var/tmp/haebancheon-frame-v10}
baseline=${BASELINE_BINARY:-/var/tmp/haebancheon-dirty-v9/host3/boayo-native-host}
candidate=${CANDIDATE_BINARY:-$work/final-host2/boayo-native-host}
assets=${ASSETS:-/var/tmp/haebancheon-motion-v7/release/data-root/boayo-assets}
plans=${PLANS:-/var/tmp/haebancheon-motion-v7/release/data-root/boayo-plans}
mkdir -p "$work/reports-final"
snapshot=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]+".build.json"))["source"])' "$candidate")
source="$snapshot/src"
mapfile -t objects < <(find "$snapshot/../objects" -name '*.o' ! -name main.o)
for name in frame_pacer state_cache; do
  g++ -std=c++17 -O2 -g -pthread -I"$source" "$here/test_$name.cpp" "${objects[@]}" -o "$work/test_$name"
  "$work/test_$name" > "$work/reports-final/$name.log" 2>&1
done
g++ -std=c++17 -O2 -g -pthread -I"$source" "$updates/boayo-dirty-v9/tests/test_texture_pool.cpp" "${objects[@]}" -o "$work/test_texture_pool"
"$work/test_texture_pool" > "$work/reports-final/texture_pool.log" 2>&1
python3 "$here/test_pacing_service.py" "$candidate" > "$work/reports-final/pacing-service.log" 2>&1
python3 "$here/test_caption_lifecycle.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports-final/captions" > "$work/reports-final/captions.log" 2>&1
python3 "$updates/boayo-components-v6/tests/test_resident.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports-final/resident" > "$work/reports-final/resident.log" 2>&1
python3 "$updates/boayo-motion-v7/tests/test_motion_pixels.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports-final/motion" > "$work/reports-final/motion.log" 2>&1
BOAYO_ASSETS=0 BOAYO_PLANS=0 python3 "$updates/boayo-motion-v7/tests/test_motion_pixels.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports-final/no-assets" > "$work/reports-final/no-assets.log" 2>&1
python3 "$here/test_app_updates.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports-final/app-updates" > "$work/reports-final/app-updates.log" 2>&1
python3 "$updates/boayo-view-v8/tests/test_view_service.py" "$candidate" /var/tmp/haebancheon-view-v8/view-path.bin > "$work/reports-final/view-service.log" 2>&1
echo 'PASS frame-v10 regression suite'
