#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")";pwd)
repo=$(cd "$here/../..";pwd)
base=${BASE_BUILD:-/var/tmp/haebancheon-l1-16k-dma3/release}
work=${1:-/var/tmp/haebancheon-components-v6}
out="$work/release"
test "$(realpath -m "$base")" != "$(realpath -m "$out")"
mkdir -p "$work/reports" "$out/reports" "$out/firmware" "$out/kernel" "$out/data-root"
if [[ ${REUSE_BUILDS:-0} != 1 ]]; then
 bash "$here/software/boayo/native/build.sh" "$work/host" host > "$work/reports/host-build.log" 2>&1
 python3 "$here/tests/make_assets.py" "$work/host/boayo-native-host" "$out/data-root/boayo-assets" > "$work/reports/assets.log" 2>&1
 python3 "$here/tests/make_plans.py" "$work/host/boayo-native-host" "$out/data-root/boayo-plans" "$here/asset_catalog.json" > "$work/reports/plans.log" 2>&1
 bash "$here/software/boayo/native/build.sh" "$work/apps" rv32 > "$work/reports/rv32-build.log" 2>&1
fi
if [[ ! -d "$out/root" ]]; then cp -a "$base/root" "$out/root";fi
toolchain=/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl
"$toolchain-strip" -o "$out/root/usr/bin/boayo-native" "$work/apps/boayo-native-rv32"
python3 "$here/install_assets.py" "$out/data-root/boayo-assets" "$out/root"
python3 "$here/install_plans.py" "$out/data-root/boayo-plans" "$out/root" > "$work/reports/install-plans.log"
printf 'HAEBANCHEON-BoAYO-components-v6 / L1-16K DMA3 / shared controls and DMA app recipes / physical verification pending\n' > "$out/root/etc/oryukdo-image"
cp "$here/asset_catalog.json" "$out/root/usr/share/boayo/asset_catalog.json"
python3 "$here/root_list.py" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$base/linux/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-haebancheon-components6'
export KBUILD_BUILD_TIMESTAMP='2026-11-14 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
# The cache/coherent DMA3 kernel sources and drivers are preserved unchanged.
make -C "$base/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$base/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp "$repo/variants/l1-16k-dma3-lru-ram/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
python3 "$repo/variants/l1-16k-dma3-lru-ram/software/storage/make_image.py" "$out/image" --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
mv "$out/image/ORYUKDO-KFB-SD4-DMA-v1.img" "$out/image/HAEBANCHEON-KFB-BoAYO-components-v6.img"
python3 - "$out/image/manifest.json" "$out/image/HAEBANCHEON-KFB-BoAYO-components-v6.img" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);m=json.loads(p.read_text());m['image']=sys.argv[2]
m['requires']='HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit, unchanged RTL'
p.write_text(json.dumps(m,indent=2)+'\n')
PY
echo 'PASS image built; physical board verification remains separate'
