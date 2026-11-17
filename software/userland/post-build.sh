#!/bin/sh
set -eu
target=$1
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
bosio="$here/../../variants/l1-16k-dma3-lru-ram/software/bosio"
boayo="$here/../../updates/boayo-tilebatch-v14/software/boayo/native"
cc="$HOST_DIR/bin/riscv32-buildroot-linux-musl-gcc"
triple=$($cc -dumpmachine)
version=$($cc -dumpfullversion)
# Buildroot removes headers and static archives at target-finalize. Restore
# the target development files here, never host binaries/libraries.
mkdir -p "$target/usr/include" "$target/usr/lib/gcc/$triple/$version"
cp -a "$STAGING_DIR/usr/include/." "$target/usr/include/"
# GCC installs its target C++ headers beside the sysroot, not inside it.
mkdir -p "$target/usr/include/c++"
cp -a "$HOST_DIR/$triple/include/c++/." "$target/usr/include/c++/"
for dir in lib usr/lib; do
    find "$STAGING_DIR/$dir" -maxdepth 1 \( -name '*.a' -o -name '*crt*.o' \) \
        -exec cp -a -t "$target/$dir" {} +
done
for name in libgcc.a libgcc_eh.a crtbegin.o crtbeginS.o crtbeginT.o crtend.o crtendS.o; do
    source=$($cc -print-file-name="$name")
    test -f "$source"
    cp -a "$source" "$target/usr/lib/gcc/$triple/$version/$name"
done
mkdir -p "$target/data/apps" "$target/data/src" "$target/dev/pts"
chmod 0755 "$target/usr/bin/userland-check" "$target/usr/bin/app-install"
"$cc" -Os -Wall -Wextra -Werror -s -o "$target/usr/bin/bosioctl" "$bosio/tools/bosioctl.c"
install -m 0644 "$bosio/include/bosio_uapi.h" "$target/usr/include/bosio_uapi.h"
# Use the same UAPI as the kernel and the same cross toolchain as the rootfs.
(
    build=$(mktemp -d)
    trap 'rm -rf -- "$build"' EXIT
    RV32_CXX="$HOST_DIR/bin/$triple-g++" BOSIO_UAPI="$bosio/include/bosio_uapi.h" \
        bash "$boayo/build.sh" "$build" rv32
    install -m 0755 "$build/boayo-native-rv32" "$target/usr/bin/bosio-tiles"
    "$HOST_DIR/bin/$triple-strip" "$target/usr/bin/bosio-tiles"
    ln -sfn bosio-tiles "$target/usr/bin/boayo"
    ln -sfn bosio-tiles "$target/usr/bin/boayo-native"
)
install -D -m 0644 "$boayo/vendor/json-LICENSE.MIT" "$target/usr/share/licenses/boayo/json-LICENSE.MIT"
# tmpfs /tmp is bounded; native builds should use persistent /data/tmp.
mkdir -p "$target/data/tmp"
chmod 1777 "$target/data/tmp"
printf 'HAEBANCHEON RV32IMA development rootfs / Buildroot 2025.02.19\n' > "$target/etc/oryukdo-image"
