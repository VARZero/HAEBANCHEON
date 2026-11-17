#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
work=${1:-/var/tmp/haebancheon-tilebatch-v14}
base=${BASE_BUILD:-/var/tmp/haebancheon-l1-16k-dma3/release}
previous=${PREVIOUS_BUILD:-/var/tmp/haebancheon-geometry-v13/release}
out="${IMAGE_BUILD:-$work/release}"
test ! -e "$out/image/HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img"
mkdir -p "$out/firmware" "$out/kernel" "$out/data-root"
if [[ ! -d "$out/linux" ]];then cp -a --reflink=auto "$base/linux" "$out/linux";fi
cp -a "$here/software/bosio/." "$out/linux/drivers/misc/oryukdo-bosio/"
test -f "$work/rv32/boayo-native-rv32"
if [[ ! -d "$out/root" ]];then cp -a "$previous/root" "$out/root";fi
cp -a "$previous/data-root/." "$out/data-root/"
/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip -o "$out/root/usr/bin/boayo-native" "$work/rv32/boayo-native-rv32"
# Preserve the exact earlier software for controlled comparisons on the new kernel.
cp /var/tmp/haebancheon-frame-v10/boayo-native-v10 "$out/root/usr/bin/boayo-native-v10"
printf 'HAEBANCHEON-BoAYO-tilebatch-v14 / L1-16K DMA3 / 32x32 source damage, cached assets, batched copies, rectangular app bodies\n' > "$out/root/etc/oryukdo-image"
# Replace only this new build's asset/plan directories; preserve the v13 tree.
python3 - "$out" "$work" <<'PYASSETS'
from pathlib import Path
import shutil,sys
out,work=map(Path,sys.argv[1:])
for sub in ['root/usr/share/boayo/assets','root/usr/share/boayo/plans','data-root/boayo-assets','data-root/boayo-plans']:
 target=(out/sub).resolve()
 assert target.is_relative_to(out.resolve()) and target!=out.resolve()
 if target.exists():shutil.rmtree(target)
shutil.copytree(work/'assets',out/'data-root/boayo-assets')
shutil.copytree(work/'plans',out/'data-root/boayo-plans')
PYASSETS
python3 "$here/../boayo-motion-v7/install_assets.py" "$work/assets" "$out/root"
python3 "$here/../boayo-motion-v7/install_plans.py" "$work/plans" "$out/root"
python3 "$here/../boayo-motion-v7/root_list.py" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$out/linux/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-haebancheon-tilebatch14'
export KBUILD_BUILD_TIMESTAMP='2026-11-15 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp "$repo/variants/l1-16k-dma3-lru-ram/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
python3 "$repo/variants/l1-16k-dma3-lru-ram/software/storage/make_image.py" "$out/image" --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
mv "$out/image/ORYUKDO-KFB-SD4-DMA-v1.img" "$out/image/HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img"
python3 - "$out/image" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]); f=p/'HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img'
m=json.loads((p/'manifest.json').read_text());m.update(image=str(f),sha256=hashlib.file_digest(f.open('rb'),'sha256').hexdigest(),requires='HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit, unchanged RTL',hardware_tested=False)
(p/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
PY
echo 'IMAGE_BUILT; validation and physical board measurement still required'
