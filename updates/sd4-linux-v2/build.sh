#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")"; pwd)
repo=$(cd "$here/../.."; pwd)
base=${BASE_BUILD:-/var/tmp/oryukdo-sd4-linux}
out=${1:-/var/tmp/oryukdo-sd4-linux-v2}
root_list=${ROOT_LIST_TOOL:-/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/bosio-linux-sw/software/boayo/root_list.py}
test "$(realpath -m "$base")" != "$(realpath -m "$out")"
test -f "$base/kernel/.config"
test -f "$root_list"
mkdir -p "$out"
if [[ ! -f "$out/linux/Makefile" ]]; then
 cp -aL --reflink=auto "$base/linux" "$out/linux"
fi
if [[ ! -d "$out/root" ]]; then cp -a "$base/root" "$out/root"; fi
if [[ -n "$(find "$out/linux" -type l -lname '/*' -print -quit)" ]]; then
 echo 'External Linux source symlink rejected' >&2; exit 1
fi
mkdir -p "$out/kernel" "$out/firmware" "$out/data-root" "$out/reports"
python3 "$here/tests/run_driver.py" > "$out/reports/driver_test.log" 2>&1
cp "$here/oryukdo_sd.c" "$out/linux/drivers/mmc/host/oryukdo_sd.c"
python3 "$here/prepare_root.py" "$out/root"
python3 "$root_list" "$out/root" > "$out/root.list"
cp "$base/kernel/.config" "$out/kernel/.config"
"$out/linux/scripts/config" --file "$out/kernel/.config" \
 --set-str INITRAMFS_SOURCE "$out/root.list" --set-str LOCALVERSION '-oryukdo-sd4-dma2'
export KBUILD_BUILD_TIMESTAMP='2026-11-10 00:00:00 UTC' KBUILD_BUILD_USER=oryukdo KBUILD_BUILD_HOST=kfb
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$out/config.log" 2>&1
make -C "$out/linux" O="$out/kernel" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j6 Image > "$out/kernel.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$base/firmware/gen3.dts" "$out/firmware/"
printf 'ORYUKDO SD4 DMA v2 persistent data partition\n' > "$out/data-root/README.txt"
python3 "$repo/variants/sd4-dma/software/storage/make_image.py" "$out/image" \
 --firmware "$out/firmware" --data-root "$out/data-root" > "$out/image.log" 2>&1
python3 "$here/verify_package.py" "$base" "$out" > "$out/reports/package.log"
echo 'PASS v2 kernel/image built; unchanged FPGA bitstream; physical MMC/ext4 retest pending'
