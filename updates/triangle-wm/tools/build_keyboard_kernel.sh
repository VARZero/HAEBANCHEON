#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-/var/tmp/triangle-keyboard-v16}
base=/var/tmp/triangle-publish-copy
source_base=/var/tmp/haebancheon-userland-validation/sources/linux-6.12
mkdir -p "$out/firmware"
if [[ ! -d "$out/linux" ]]; then cp -a --reflink=auto "$source_base" "$out/linux"; fi
if [[ ! -d "$out/kernel" ]]; then cp -a --reflink=auto "$base/kernel" "$out/kernel"; fi
rm -f "$out/kernel/source"
ln -s "$out/linux" "$out/kernel/source"
src=$out/linux
cp "$repo/updates/triangle-wm/linux/haebancheon_peripherals.c" "$src/drivers/misc/haebancheon_peripherals.c"
if ! grep -q 'HAEBANCHEON_PERIPHERALS' "$src/drivers/misc/Kconfig"; then
 cat >> "$src/drivers/misc/Kconfig" <<'EOF'

config HAEBANCHEON_PERIPHERALS
 bool "HAEBANCHEON existing polling peripherals"
 depends on SERIAL_8250 && SPI_MASTER && I2C
EOF
 echo 'obj-$(CONFIG_HAEBANCHEON_PERIPHERALS) += haebancheon_peripherals.o' >> "$src/drivers/misc/Makefile"
fi
python3 - "$src" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'drivers/spi/spidev.c';s=p.read_text()
anchor='static const struct of_device_id spidev_dt_ids[] = {'
if 'oryukdo,esp32-keyboard' not in s:s=s.replace(anchor,anchor+'\n\t{ .compatible = "oryukdo,esp32-keyboard", .data = &spidev_of_check },')
assert 'oryukdo,esp32-keyboard' in s;p.write_text(s)
PY
cfg=("$src/scripts/config" --file "$out/kernel/.config")
"${cfg[@]}" -e INPUT -e INPUT_EVDEV -e INPUT_MISC -e INPUT_UINPUT -e SPI -e SPI_MASTER -e SPI_SPIDEV -e I2C -e I2C_CHARDEV -e SERIAL_8250 -e HAEBANCHEON_PERIPHERALS --set-val SERIAL_8250_NR_UARTS 1 --set-val SERIAL_8250_RUNTIME_UARTS 1 -d SERIAL_OF_PLATFORM -d SERIAL_8250_CONSOLE --set-str LOCALVERSION '-haebancheon-keyboard'
export KBUILD_BUILD_TIMESTAMP='2026-11-27 00:00:00 UTC' KBUILD_BUILD_USER=haebancheon KBUILD_BUILD_HOST=build KBUILD_BUILD_VERSION=1
cross=/var/tmp/haebancheon-userland-validation/buildroot/host/bin/riscv32-buildroot-linux-musl-
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" olddefconfig > "$out/kernel-config.log" 2>&1
make -C "$src" O="$out/kernel" ARCH=riscv CROSS_COMPILE="$cross" -j6 Image > "$out/kernel-build.log" 2>&1
cp "$out/kernel/arch/riscv/boot/Image" "$out/firmware/Image"
cp "$base/firmware/fw_jump.bin" "$out/firmware/"
python3 - "$repo" "$out" <<'PY'
from pathlib import Path
import sys
repo,out=map(Path,sys.argv[1:]);s=(repo/'variants/l1-16k-dma3-lru-ram/software/bosio/gen3-bosio.dts').read_text()
s=s.replace('    soc {','    aliases { serial0 = &hb_uart; spi0 = &hb_spi; i2c0 = &hb_i2c; };\n    soc {',1)
anchor='        timer@200bff8 {';assert s.count(anchor)==1
s=s.replace(anchor,(repo/'updates/triangle-wm/linux/peripherals.dtsi').read_text()+'\n'+anchor)
(out/'firmware/gen3.dts').write_text(s)
PY
dtc -I dts -O dtb -o "$out/firmware/gen3.dtb" "$out/firmware/gen3.dts" > "$out/dtc.log" 2>&1
echo KEYBOARD_KERNEL_BUILT
