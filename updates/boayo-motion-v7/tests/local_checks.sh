#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")/..";pwd)
work=${1:-/var/tmp/haebancheon-motion-v7}
assets="$work/release/data-root/boayo-assets"
plans="$work/release/data-root/boayo-plans"
if [[ ${LOCAL_PHASE:-all} != image ]]; then
for mode in host sanitize; do
 binary="$work/$mode/boayo-native-$mode"
 BOAYO_PLAN_DIR="$plans" BOAYO_ASSET_DIR="$assets" python3 "$here/tests/test_motion.py" "$binary" "$work/reports/motion-$mode" > "$work/reports/motion-$mode.log" 2>&1
 python3 "$here/tests/test_pose.py" "$binary" "$work/reports/pose-$mode" > "$work/reports/pose-$mode.log" 2>&1
 suffix="";if [[ $mode == sanitize ]]; then suffix=-sanitize;fi
 python3 "$here/tests/test_components.py" /var/tmp/oryukdo-boayo-tiles-v5/host/boayo-native-host "$binary" "$assets" "$plans" "$work/reports/components$suffix" > "$work/reports/components$suffix.log" 2>&1
done
python3 "$here/tests/test_install_assets.py" "$assets" > "$work/reports/build-asset-gate.log" 2>&1
python3 "$here/tests/test_install_plans.py" "$plans" > "$work/reports/build-plan-gate.log" 2>&1
fi
if [[ ${LOCAL_PHASE:-all} == host ]]; then exit 0;fi
python3 "$here/tests/verify_build.py" "$work/release" /var/tmp/haebancheon-l1-16k-dma3/release "$work/reports/artifact.json" > "$work/reports/artifact.log" 2>&1
bash "$here/tests/test_component_scenes.sh" "$work"
bash "$here/tests/test_sd_image.sh" "$work"
