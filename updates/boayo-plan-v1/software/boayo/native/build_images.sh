#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
project=$(cd -- "$here/../../.." && pwd)
out=${1:-/var/tmp/oryukdo-boayo-native}
src=/var/tmp/oryukdo-boayo-boottrace-v1/linux
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
python3 "$here/prepare_image.py" "$out"
python3 "$here/../root_list.py" "$out/root" > "$out/root.list"
mkdir -p "$out/kernel" "$out/firmware" "$out/images"
cp /var/tmp/oryukdo-boayo-boottrace-v1/kernel/.config "$out/kernel/.config"
"$src/scripts/config" --file "$out/kernel/.config" --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-oryukdo-kfb-native2'
cp "$project/software/firmware/fw_jump.bin" "$out/firmware/"
cp "$project/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
export KBUILD_BUILD_TIMESTAMP='2026-11-09 00:00:00 UTC' KBUILD_BUILD_USER=oryukdo KBUILD_BUILD_HOST=kfb KBUILD_BUILD_VERSION=1
for compression in gzip raw; do
 if [[ "$compression" == gzip ]]; then
  "$src/scripts/config" --file "$out/kernel/.config" --enable INITRAMFS_COMPRESSION_GZIP --disable INITRAMFS_COMPRESSION_NONE
 else
  "$src/scripts/config" --file "$out/kernel/.config" --disable INITRAMFS_COMPRESSION_GZIP --enable INITRAMFS_COMPRESSION_NONE
 fi
 make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/kernel_config_$compression.log" 2>&1
 make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 Image > "$out/kernel_$compression.log" 2>&1
 cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
 mkdir -p "$out/$compression"
 python3 "$project/soc/board/make_sd.py" "$out/$compression" --firmware "$out/firmware" > "$out/sd_$compression.log"
 image="$out/images/ORYUKDO-KFB-BoAYO-native-v2-$compression.img"
 candidate="$out/$compression/ORYUKDO-Gen3-BOSIO-KFB-1GiB-BusyBox.img"
 python3 "$project/soc/board/verify_image.py" "$candidate" "$out/firmware" "$out/$compression/gen3.dtb" > "$out/verify_$compression.log"
 mv "$candidate" "$image"
 cp "$out/kernel/.config" "$out/$compression/kernel.config"
 cp "$out/kernel/System.map" "$out/$compression/System.map"
 sha256sum "$image"
done
echo 'BUILT_NATIVE_SD_CANDIDATES hardware not tested'
