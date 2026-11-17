#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
repo=$(cd "$here/../.."; pwd)
work=${1:-/var/tmp/oryukdo-boayo-tiles-v1}
base=${BASE_BUILD:-/var/tmp/oryukdo-boayo-rgbdma-v1/release}
old=${RAW_BASELINE:-/var/tmp/oryukdo-boayo-rawdma-v1/host/boayo-native-host}
rgb=${RGB_BASELINE:-/var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host}
native="$here/software/boayo/native"
plan_base=${PLAN_BASELINE:-/var/tmp/oryukdo-boayo-plan-v1/host/boayo-native-host}
assets="$work/release/data-root/boayo-assets"
plans="$work/release/data-root/boayo-plans"
image="$work/release/image/ORYUKDO-KFB-BoAYO-tiles-v1.img"
mkdir -p "$work/reports"
BOAYO_PLANS=0 BOAYO_ASSETS=0 python3 "$native/tests/run_suite.py" "$work/host/boayo-native-host" "$work/reports/host"
BOAYO_PLAN_DIR="$plans" BOAYO_ASSET_DIR="$assets" python3 "$native/tests/run_suite.py" "$work/host/boayo-native-host" "$work/reports/host-assets"
python3 "$here/tests/test_assets.py" "$rgb" "$work/host/boayo-native-host" "$assets" "$work/reports/assets" > "$work/reports/assets-test.log" 2>&1
python3 "$here/tests/test_plans.py" "$old" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/plans" > "$work/reports/plans-test.log" 2>&1
python3 "$here/tests/test_resident.py" "$plan_base" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/resident" > "$work/reports/resident-test.log" 2>&1
bash "$native/build.sh" "$work/sanitize" sanitize > "$work/reports/sanitize-build.log" 2>&1
python3 "$here/tests/test_assets.py" "$rgb" "$work/sanitize/boayo-native-sanitize" "$assets" "$work/reports/assets-sanitize" > "$work/reports/assets-sanitize.log" 2>&1
python3 "$here/tests/test_plans.py" "$old" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/plans-sanitize" > "$work/reports/plans-sanitize.log" 2>&1
python3 "$here/tests/test_resident.py" "$plan_base" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/resident-sanitize" > "$work/reports/resident-sanitize.log" 2>&1
python3 "$here/tests/verify_build.py" "$work/release" "$base" "$work/reports/artifact.json" > "$work/reports/artifact.log" 2>&1
python3 "$here/tests/test_image.py" "$image" "$work/reports/qemu" > "$work/reports/qemu-test.log" 2>&1
bash "$here/tests/test_sd_image.sh" "$work"
echo 'PASS local validation; physical board and full CPU RTL boot remain separate'
