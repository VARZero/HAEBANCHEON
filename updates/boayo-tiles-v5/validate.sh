#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
repo=$(cd "$here/../.."; pwd)
work=${1:-/var/tmp/oryukdo-boayo-tiles-v5}
base=${BASE_BUILD:-/var/tmp/oryukdo-boayo-rgbdma-v1/release}
old=${RAW_BASELINE:-/var/tmp/oryukdo-boayo-rawdma-v1/host/boayo-native-host}
rgb=${RGB_BASELINE:-/var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host}
native="$here/software/boayo/native"
plan_base=${PLAN_BASELINE:-/var/tmp/oryukdo-boayo-plan-v1/host/boayo-native-host}
assets="$work/release/data-root/boayo-assets"
plans="$work/release/data-root/boayo-plans"
image="$work/release/image/ORYUKDO-KFB-BoAYO-tiles-v5.img"
mkdir -p "$work/reports"
if [[ ${VALIDATE_PHASE:-all} != image ]]; then
python3 "$here/tests/test_ready.py" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/device-ready" > "$work/reports/device-ready.log" 2>&1
g++ -std=c++17 -O2 "$here/tests/test_check_allocations.cpp" -o "$work/check-allocations"
"$work/check-allocations" > "$work/reports/check-allocations.log"
BOAYO_VALIDATE_SCENE=1 python3 "$here/tests/test_versions.py" "$plan_base" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/versions-debug" > "$work/reports/versions-debug.log" 2>&1
BOAYO_PLANS=0 BOAYO_ASSETS=0 python3 "$native/tests/run_suite.py" "$work/host/boayo-native-host" "$work/reports/host"
BOAYO_PLAN_DIR="$plans" BOAYO_ASSET_DIR="$assets" python3 "$native/tests/run_suite.py" "$work/host/boayo-native-host" "$work/reports/host-assets"
python3 "$here/tests/test_assets.py" "$rgb" "$work/host/boayo-native-host" "$assets" "$work/reports/assets" > "$work/reports/assets-test.log" 2>&1
python3 "$here/tests/test_plans.py" "$old" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/plans" > "$work/reports/plans-test.log" 2>&1
python3 "$here/tests/test_resident.py" "$plan_base" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/resident" > "$work/reports/resident-test.log" 2>&1
bash "$native/build.sh" "$work/sanitize" sanitize > "$work/reports/sanitize-build.log" 2>&1
python3 "$here/tests/test_ready.py" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/device-ready-sanitize" > "$work/reports/device-ready-sanitize.log" 2>&1
python3 "$here/tests/test_assets.py" "$rgb" "$work/sanitize/boayo-native-sanitize" "$assets" "$work/reports/assets-sanitize" > "$work/reports/assets-sanitize.log" 2>&1
python3 "$here/tests/test_plans.py" "$old" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/plans-sanitize" > "$work/reports/plans-sanitize.log" 2>&1
python3 "$here/tests/test_resident.py" "$plan_base" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/resident-sanitize" > "$work/reports/resident-sanitize.log" 2>&1
python3 "$here/tests/test_versions.py" "$plan_base" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/versions" > "$work/reports/versions.log" 2>&1
python3 "$here/tests/test_versions.py" "$plan_base" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/versions-sanitize" > "$work/reports/versions-sanitize.log" 2>&1
python3 "$here/tests/test_install_plans.py" "$plans" > "$work/reports/build-plan-gate.log" 2>&1
fi
if [[ ${VALIDATE_PHASE:-all} != host ]]; then
python3 "$here/tests/verify_build.py" "$work/release" "$base" "$work/reports/artifact.json" > "$work/reports/artifact.log" 2>&1
python3 "$here/tests/test_image.py" "$image" "$work/reports/qemu" > "$work/reports/qemu-test.log" 2>&1
python3 "$here/tests/summarize_publish.py" "$work/reports/qemu/uart.log" "$work/reports/publish-profile-qemu.json" qemu > "$work/reports/publish-profile-qemu.log"
python3 "$here/tests/summarize_frame.py" "$work/reports/qemu/uart.log" "$work/reports/frame-profile-qemu.json" qemu > "$work/reports/frame-profile-qemu.log"
bash "$here/tests/test_scene_switch.sh" "$work"
bash "$here/tests/test_sd_image.sh" "$work"
fi
echo 'PASS selected local validation phase; physical board and full CPU RTL boot remain separate'
