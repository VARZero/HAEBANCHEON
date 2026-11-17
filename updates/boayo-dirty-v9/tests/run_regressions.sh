#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")";pwd)
updates=$(cd "$here/../..";pwd)
work=${1:-/var/tmp/haebancheon-dirty-v9}
baseline=${BASELINE_BINARY:-/var/tmp/haebancheon-view-v8/host3/boayo-native-host}
assets=${ASSETS:-/var/tmp/haebancheon-motion-v7/release/data-root/boayo-assets}
plans=${PLANS:-/var/tmp/haebancheon-motion-v7/release/data-root/boayo-plans}
candidate="$work/host3/boayo-native-host"
mkdir -p "$work/reports" "$work/tests"
cp "$here/test_texture_pool.cpp" "$work/tests/"
python3 "$here/run_local.py" "$work" > "$work/pool.log" 2>&1
python3 "$updates/boayo-components-v6/tests/test_resident.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports/resident" > "$work/resident.log" 2>&1
python3 "$updates/boayo-motion-v7/tests/test_motion_pixels.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports/motion-pixels" > "$work/motion-pixels.log" 2>&1
BOAYO_ASSETS=0 BOAYO_PLANS=0 python3 "$updates/boayo-motion-v7/tests/test_motion_pixels.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports/no-assets" > "$work/no-assets.log" 2>&1
python3 "$here/test_app_traffic.py" "$baseline" "$candidate" "$assets" "$plans" "$work/reports/app-traffic" > "$work/app-traffic.log" 2>&1
python3 "$updates/boayo-view-v8/tests/test_view_service.py" "$candidate" "${VIEW_PATH:-/var/tmp/haebancheon-view-v8/view-path.bin}" > "$work/view-service.log" 2>&1
echo 'PASS local regressions; hardware measurements remain separate'
