#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$repo"
platform=${1:-/var/tmp/triangle-keyboard-v16}
out=${2:-/var/tmp/triangle-keyboard-v16/release}
mkdir -p "$platform/images"
cp --reflink=auto --sparse=always /var/tmp/triangle-publish-copy/rootfs.ext4 "$platform/rootfs.ext4"
python3 variants/l1-16k-dma3-lru-ram/soc/board/make_sd.py "$platform/images/boot" --firmware "$platform/firmware"
python3 variants/l1-16k-dma3-lru-ram/soc/board/verify_image.py "$platform/images/boot/ORYUKDO-Gen3-BOSIO-KFB-1GiB-BusyBox.img" "$platform/firmware" "$platform/images/boot/gen3.dtb"
TRIANGLE_IMAGE_PLATFORM="$platform" python3 updates/triangle-wm/tools/package_fixed.py --build "$platform/userland" --output "$out" --name HAEBANCHEON-keyboard-v16.img
