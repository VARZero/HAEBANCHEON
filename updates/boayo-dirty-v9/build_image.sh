#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")";pwd)
repo=$(cd "$here/../..";pwd)
work=${1:-/var/tmp/haebancheon-dirty-v9}
base=${BASE_BUILD:-/var/tmp/haebancheon-l1-16k-dma3/release}
previous=${PREVIOUS_BUILD:-/var/tmp/haebancheon-motion-v7/release}
out="$work/release"
test "$(realpath -m "$out")" != "$(realpath -m "$previous")"
test ! -e "$out/image/HAEBANCHEON-KFB-BoAYO-dirty-v9.img"
mkdir -p "$out/firmware" "$out/kernel" "$out/data-root"
if [[ ! -f "$work/rv32-final/boayo-native-rv32" ]]; then
 bash "$here/software/boayo/native/build.sh" "$work/rv32-final" rv32
fi
# Preserve the already validated cache/DMA kernel, boot firmware, assets and plans.
test -d "$previous/root"
if [[ ! -d "$out/root" ]]; then cp -a "$previous/root" "$out/root";fi
cp -a "$previous/data-root/." "$out/data-root/"
/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip -o "$out/root/usr/bin/boayo-native" "$work/rv32-final/boayo-native-rv32"
printf 'HAEBANCHEON-BoAYO-dirty-v9 / L1-16K DMA3 / persistent window textures and dirty rectangles / physical verification pending\n' > "$out/root/etc/oryukdo-image"
python3 "$here/../boayo-motion-v7/root_list.py" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$base/linux/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-haebancheon-dirty9'
export KBUILD_BUILD_TIMESTAMP='2026-11-14 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
make -C "$base/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$base/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp "$repo/variants/l1-16k-dma3-lru-ram/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
python3 "$repo/variants/l1-16k-dma3-lru-ram/software/storage/make_image.py" "$out/image" --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
mv "$out/image/ORYUKDO-KFB-SD4-DMA-v1.img" "$out/image/HAEBANCHEON-KFB-BoAYO-dirty-v9.img"
python3 - "$out/image" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]); f=p/'HAEBANCHEON-KFB-BoAYO-dirty-v9.img'
m=json.loads((p/'manifest.json').read_text());m.update(image=str(f),sha256=hashlib.file_digest(f.open('rb'),'sha256').hexdigest(),requires='HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit, unchanged RTL',hardware_tested=False)
(p/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
PY
echo 'PASS image built; physical board verification remains separate'
