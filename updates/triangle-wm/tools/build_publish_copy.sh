#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
base=/var/tmp/haebancheon-userland-validation
out=/var/tmp/triangle-publish-copy
src=$base/sources/linux-6.12
cd "$repo"
mkdir -p "$out/firmware"
if [[ ! -d "$out/kernel" ]]; then cp -a --reflink=auto "$base/kernel" "$out/kernel"; fi
for f in common/bosio_engine.c common/bosio_engine.h include/bosio_uapi.h driver/bosio_kfb.c; do
 cp "variants/l1-16k-dma3-lru-ram/software/bosio/$f" "$src/drivers/misc/oryukdo-bosio/$f"
done
"$src/scripts/config" --file "$out/kernel/.config" --set-str LOCALVERSION '-haebancheon-publishcopy'
export KBUILD_BUILD_TIMESTAMP='2026-11-22 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=build KBUILD_BUILD_VERSION=1
cross=$base/buildroot/host/bin/riscv32-buildroot-linux-musl-
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" olddefconfig > "$out/config.log" 2>&1
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" -j4 Image > "$out/build.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
cp variants/l1-16k-dma3-lru-ram/software/bosio/gen3-bosio.dts "$out/firmware/gen3.dts"
echo KERNEL_BUILT
