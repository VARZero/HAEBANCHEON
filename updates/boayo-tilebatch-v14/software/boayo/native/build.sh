#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-/var/tmp/oryukdo-boayo-native}
mode=${2:-host}
mkdir -p "$out"
snapshot=$(mktemp -d "$out/source-$mode-XXXXXX")
source="$snapshot/software/boayo/native"
mkdir -p "$source/reference/wm/native" "$snapshot/software/bosio/include"
cp -a "$here/src" "$here/vendor" "$source/"
cp "$here/reference/wm/native/bosio_compositor.cpp" "$source/reference/wm/native/"
cp "${BOSIO_UAPI:-$here/../../bosio/include/bosio_uapi.h}" "$snapshot/software/bosio/include/bosio_uapi.h"
python3 "$here/plan_revision.py" "$source/src/plan_revision.hpp"
echo "BUILD_SOURCE $source"
compiler=g++
flags=(-std=c++17 -O2 -g -ffp-contract=off -pthread)
if [[ "$mode" == rv32 ]]; then
 compiler=${RV32_CXX:-/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++}
 flags+=(-static -march=rv32ima_zicsr_zifencei -mabi=ilp32 -ffunction-sections -fdata-sections -Wl,--gc-sections)
fi
if [[ "$mode" == sanitize ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
# Independent translation units use the same flags; bound concurrency to limit RAM.
python3 - "$compiler" "$source" "$out/boayo-native-$mode" "${flags[@]}" <<'PYBUILD'
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,sys
compiler,source,target,*flags=sys.argv[1:]
source=Path(source);objects=source.parent/'objects';objects.mkdir(exist_ok=True)
units=[source/'src'/(n+'.cpp') for n in ('resident','plan','assets','geometry','wm','ui','backend','apps','app_process','runtime','selftest','main')]
units.append(source/'reference/wm/native/bosio_compositor.cpp')
def compile_unit(p):
    obj=objects/(p.stem+'.o')
    subprocess.run([compiler,*flags,'-Wframe-larger-than=32768','-c',str(p),'-o',str(obj)],check=True)
    return str(obj)
with ThreadPoolExecutor(max_workers=3) as pool: objs=list(pool.map(compile_unit,units))
subprocess.run([compiler,*flags,*objs,'-o',target],check=True)
PYBUILD
python3 "$here/build_manifest.py" "$source" "$out/boayo-native-$mode"
echo "BUILD_OK $out/boayo-native-$mode"
