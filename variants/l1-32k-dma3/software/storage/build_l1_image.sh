#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")";pwd)
project=$(cd "$here/../..";pwd)
repo=$(cd "$project/../..";pwd)
base=${BASE_BUILD:-/var/tmp/oryukdo-boayo-tiles-v5/release}
out=${1:-/var/tmp/haebancheon-l1-dma3/release}
test "$(realpath -m "$base")" != "$(realpath -m "$out")"
mkdir -p "$out/reports" "$out/kernel" "$out/firmware"
if [[ ! -f "$out/linux/Makefile" ]]; then cp -aL --reflink=auto "$base/linux" "$out/linux";fi
if [[ ! -d "$out/root" ]]; then cp -a "$base/root" "$out/root";fi
if [[ ! -d "$out/data-root" ]]; then cp -a "$base/data-root" "$out/data-root";fi
if [[ -n "$(find "$out/linux" -type l -lname '/*' -print -quit)" ]]; then echo 'external source links rejected';exit 1;fi
for name in driver common include;do cp -a "$project/software/bosio/$name" "$out/linux/drivers/misc/oryukdo-bosio/";done
cp "$here/oryukdo_sd.c" "$out/linux/drivers/mmc/host/oryukdo_sd.c"
cp "$here/oryukdo_dma.c" "$out/linux/drivers/dma/oryukdo_dma.c"
printf 'HAEBANCHEON-L1-32K-DMA3-v1 / BoAYO tiles-v5 / coherent write-through L1 / three DMA engines / physical test pending\n' > "$out/root/etc/oryukdo-image"
python3 "$repo/updates/boayo-tiles-v5/root_list.py" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$out/linux/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-haebancheon-l1dma3-v1'
export KBUILD_BUILD_TIMESTAMP='2026-11-13 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp "$project/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
python3 "$here/make_image.py" "$out/image" --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
mv "$out/image/ORYUKDO-KFB-SD4-DMA-v1.img" "$out/image/HAEBANCHEON-KFB-L1-32K-DMA3-v1.img"
python3 - "$out/image" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1])/'manifest.json'
m=json.loads(p.read_text())
m['image']=str(p.parent/'HAEBANCHEON-KFB-L1-32K-DMA3-v1.img')
m['requires']='Matched L1-32K-DMA3 bitstream; incompatible with original SD4 single-DMA bitstream'
p.write_text(json.dumps(m,indent=2)+'\n')
PY
echo 'PASS L1/DMA3 Linux image built; hardware verification separate'
