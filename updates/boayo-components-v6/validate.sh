#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
work=${1:-/var/tmp/haebancheon-components-v6}
base=${BASE_BUILD:-/var/tmp/haebancheon-l1-16k-dma3/release}
old=${CPU_BASELINE:-/var/tmp/oryukdo-boayo-tiles-v5/host/boayo-native-host}
assets="$work/release/data-root/boayo-assets"
plans="$work/release/data-root/boayo-plans"
mkdir -p "$work/reports"
python3 "$here/tests/test_components.py" "$old" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/components" > "$work/reports/components.log" 2>&1
BOAYO_PLAN_DIR="$plans" BOAYO_ASSET_DIR="$assets" python3 "$here/software/boayo/native/tests/run_suite.py" "$work/host/boayo-native-host" "$work/reports/suite" > "$work/reports/suite.log" 2>&1
bash "$here/software/boayo/native/build.sh" "$work/sanitize" sanitize > "$work/reports/sanitize-build.log" 2>&1
python3 "$here/tests/test_components.py" "$old" "$work/sanitize/boayo-native-sanitize" "$assets" "$plans" "$work/reports/components-sanitize" > "$work/reports/components-sanitize.log" 2>&1
python3 "$here/tests/test_install_assets.py" "$assets" > "$work/reports/build-asset-gate.log" 2>&1
python3 "$here/tests/test_install_plans.py" "$plans" > "$work/reports/build-plan-gate.log" 2>&1
python3 "$here/tests/make_rv32_cases.py" "$work/host/boayo-native-host" "$assets" "$plans" "$work/reports/rv32-oracle" > "$work/reports/rv32-oracle.log" 2>&1
python3 "$here/tests/verify_build.py" "$work/release" "$base" "$work/reports/artifact.json" > "$work/reports/artifact.log" 2>&1
python3 "$here/tests/test_l1_image.py" "$work/release/image/HAEBANCHEON-KFB-BoAYO-components-v6.img" "$work/reports/qemu" "$work/reports/rv32-oracle" > "$work/reports/qemu.log" 2>&1
bash "$here/tests/test_component_scenes.sh" "$work"
bash "$here/tests/test_sd_image.sh" "$work"
echo 'PASS local validation; physical board, HDMI and performance remain separate'
