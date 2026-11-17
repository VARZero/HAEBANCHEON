#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")";pwd)
project=$(cd "$here/../..";pwd)
bash "$here/test_cache.sh"
bash "$here/test.sh"
bash "$here/test_cpu.sh"
KFB_NATIVE_BACKEND=1 bash "$here/test_cpu.sh"
KFB_BUILD_DIR=/var/tmp/haebancheon-l1-bosio-integration bash "$project/soc/tools/test.sh"
KFB_NATIVE_BACKEND=1 KFB_BUILD_DIR=/var/tmp/haebancheon-l1-bosio-native-integration bash "$project/soc/tools/test.sh"
python3 "$here/collect_validation.py" "$project/provenance"
echo 'PASS L1/DMA3 RTL regressions; Linux QEMU and physical board are separate checks'
