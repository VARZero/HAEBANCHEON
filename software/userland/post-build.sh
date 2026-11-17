#!/bin/sh
set -eu
target=$1
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
# tmpfs /tmp is bounded; native builds should use persistent /data/tmp.
mkdir -p "$target/data/tmp"
chmod 1777 "$target/data/tmp"
printf 'HAEBANCHEON RV32IMA development rootfs / Buildroot 2025.02.19\n' > "$target/etc/oryukdo-image"
