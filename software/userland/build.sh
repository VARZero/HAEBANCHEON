#!/usr/bin/env bash
# All output stays in the supplied Linux filesystem directory.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
board="$repo/variants/l1-16k-dma3-lru-ram"
out=${1:?usage: build.sh OUTPUT [rootfs|all|legal-info|sdk]}
action=${2:-all}
case "$action" in rootfs|all|legal-info|sdk) ;; *) echo "Unknown action: $action" >&2; exit 2;; esac
mkdir -p "$out"
out=$(realpath "$out")
case "$out:$here" in *' '*) echo 'Build paths must not contain spaces' >&2; exit 2;; esac
case "$out" in /mnt/*) echo 'Use a Linux filesystem for OUTPUT (not /mnt/c or /mnt/d)' >&2; exit 2;; esac
export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH=1794009600
export E2FSPROGS_FAKE_TIME=$SOURCE_DATE_EPOCH
# WSL may append Windows PATH entries containing spaces, rejected by Buildroot.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
jobs=${JOBS:-6}
python3 "$here/prepare.py" buildroot "$out"
br="$out/buildroot-2025.02.19"
brmake() { make -C "$br" O="$out/buildroot" BR2_EXTERNAL="$here" BR2_DL_DIR="${DL_DIR:-$out/dl}" "$@"; }
brmake haebancheon_defconfig
if [[ -n "${APP_OVERLAY:-}" ]]; then
    overlay=$(realpath "$APP_OVERLAY")
    test -d "$overlay"
    case "$overlay" in *' '*|*'"'*|*'\\'*) echo 'APP_OVERLAY contains unsupported characters' >&2; exit 2;; esac
    python3 - "$out/buildroot/.config" "$here/overlay $overlay" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
lines = p.read_text().splitlines()
p.write_text('\n'.join('BR2_ROOTFS_OVERLAY="' + sys.argv[2] + '"'
                       if s.startswith('BR2_ROOTFS_OVERLAY=') else s for s in lines) + '\n')
PY
    brmake olddefconfig
fi
python3 "$here/tests/check_config.py" buildroot "$out/buildroot/.config"
brmake BR2_JLEVEL="$jobs"
python3 "$here/tests/check_elf.py" "$out/buildroot/target" --output "$out/buildroot/images/elf-audit.json"
if [[ "$action" == legal-info || "$action" == sdk ]]; then
    brmake "$action"
    exit
fi
[[ "$action" == all ]] || exit 0

python3 "$here/prepare.py" linux "$out"
src="$out/sources/linux-6.12"
cross="$out/buildroot/host/bin/riscv32-buildroot-linux-musl-"
# A separate static BusyBox supplies a recovery shell and switch_root. The
# full rootfs remains dynamically linked and lives on SD, outside the Image.
bb_src=$(find "$out/buildroot/build" -maxdepth 1 -type d -name 'busybox-*' | head -n 1)
test -n "$bb_src"
# Buildroot configured BusyBox in-tree; a separate O= build requires clean
# sources. Copy the already verified/patched tree, then clean only the copy.
if [[ ! -d "$out/bootstrap-source" ]]; then
    cp -a --reflink=auto "$bb_src" "$out/bootstrap-source"
    make -C "$out/bootstrap-source" distclean
fi
bb_src="$out/bootstrap-source"
mkdir -p "$out/bootstrap-build" "$out/kernel" "$out/firmware"
make -C "$bb_src" O="$out/bootstrap-build" defconfig
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$out/bootstrap-build/.config"
make -C "$bb_src" O="$out/bootstrap-build" CROSS_COMPILE="$cross" -j"$jobs"
cat > "$out/bootstrap.list" <<EOF
dir /bin 0755 0 0
dir /dev 0755 0 0
dir /proc 0555 0 0
dir /sys 0555 0 0
dir /newroot 0755 0 0
nod /dev/console 0600 0 0 c 5 1
file /bin/busybox $out/bootstrap-build/busybox 0755 0 0
file /init $here/bootstrap/init 0755 0 0
EOF
cp "$board/software/storage/linux.config" "$out/kernel/.config"
cfg=("$src/scripts/config" --file "$out/kernel/.config")
"${cfg[@]}" --set-str INITRAMFS_SOURCE "$out/bootstrap.list" \
    --set-str LOCALVERSION '-haebancheon-userland' \
    --enable BINFMT_SCRIPT --enable FILE_LOCKING --enable SHMEM --enable MEMFD_CREATE --enable TMPFS \
    --enable DEVTMPFS --enable UNIX98_PTYS --enable EXT4_FS \
    --enable MMC --enable MMC_BLOCK --enable MMC_ORYUKDO \
    --enable DMADEVICES --enable ORYUKDO_DMA --enable SIFIVE_PLIC \
    --enable VIRTIO_MENU --enable VIRTIO_MMIO --enable VIRTIO_BLK \
    --disable FPU --disable RISCV_ISA_C
export KBUILD_BUILD_TIMESTAMP='2026-11-07 00:00:00 UTC'
export KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=build KBUILD_BUILD_VERSION=1
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" olddefconfig
python3 "$here/tests/check_config.py" linux "$out/kernel/.config"
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" -j"$jobs" Image
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$board/software/firmware/fw_jump.bin" "$out/firmware/"
cp "$board/software/bosio/gen3-bosio.dts" "$out/firmware/gen3.dts"
python3 "$here/make_image.py" "$out"
echo "Built $out/images/HAEBANCHEON-userland.img; hardware validation is separate."
