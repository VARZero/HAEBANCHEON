#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
repo=$(cd "$here/../.."; pwd)
base=${BASE_BUILD:-/var/tmp/oryukdo-bosio-dma-v1/release}
out=${1:-/var/tmp/oryukdo-boayo-launchtrace-v1/release}
root_list=${ROOT_LIST_TOOL:-$here/root_list.py}
test "$(realpath -m "$base")" != "$(realpath -m "$out")"
mkdir -p "$out/reports"
python3 "$here/tests/run_driver.py" > "$out/reports/driver.log" 2>&1
bash "$here/software/boayo/native/build.sh" "$out/apps" rv32 > "$out/reports/app_build.log" 2>&1
if [[ ! -f "$out/linux/Makefile" ]]; then cp -aL --reflink=auto "$base/linux" "$out/linux"; fi
if [[ ! -d "$out/root" ]]; then cp -a "$base/root" "$out/root"; fi
if [[ -n "$(find "$out/linux" -type l -lname '/*' -print -quit)" ]]; then echo 'External source link rejected' >&2;exit 1;fi
dest="$out/linux/drivers/misc/oryukdo-bosio"
cp -a "$here/software/bosio/driver" "$here/software/bosio/common" "$here/software/bosio/include" "$dest/"
mkdir -p "$out/kernel" "$out/firmware" "$out/data-root"
toolchain=/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl
"$toolchain-strip" -o "$out/root/usr/bin/boayo-native" "$out/apps/boayo-native-rv32"
"$toolchain-gcc" -static -Os -march=rv32ima_zicsr_zifencei -mabi=ilp32 \
 "$here/tests/board_dma_check.c" -o "$out/root/bin/bosio-dma-check"
"$toolchain-strip" "$out/root/bin/bosio-dma-check"
bash "$repo/updates/sd4-linux-v2/tools/build_check.sh" "$out/root/bin/storage-check" > "$out/reports/storage_tool.log"
for name in dd sha256sum; do
 if [[ -L "$out/root/bin/$name" && "$(readlink "$out/root/bin/$name")" == busybox ]]; then unlink "$out/root/bin/$name"; fi
done
printf 'ORYUKDO-BoAYO-launchtrace-v1 / desktop stage diagnostic / physical test pending\n' > "$out/root/etc/oryukdo-image"
python3 "$here/prepare_root.py" "$out/root"
python3 "$root_list" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$out/linux/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" \
 --set-str LOCALVERSION '-oryukdo-launchtrace1'
export KBUILD_BUILD_TIMESTAMP='2026-11-10 00:00:00 UTC' KBUILD_BUILD_USER=oryukdo KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp "$here/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
printf 'ORYUKDO BoAYO launchtrace diagnostic candidate\n' > "$out/data-root/README.txt"
python3 "$repo/variants/sd4-dma/software/storage/make_image.py" "$out/image" \
 --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
mv "$out/image/ORYUKDO-KFB-SD4-DMA-v1.img" "$out/image/ORYUKDO-KFB-BoAYO-launchtrace-v1.img"
python3 - "$out/image/manifest.json" "$out/image/ORYUKDO-KFB-BoAYO-launchtrace-v1.img" <<'PY'
from pathlib import Path
import json,sys
p=Path(sys.argv[1]);d=json.loads(p.read_text());d['image']=sys.argv[2];p.write_text(json.dumps(d,indent=2)+'\n')
PY
echo 'PASS kernel/image build; bitstream unchanged; diagnostic trace physical test pending'
