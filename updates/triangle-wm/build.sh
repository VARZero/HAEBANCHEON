#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
native="$here/../boayo-tilebatch-v14/software/boayo/native/src"
uapi="$here/../../variants/l1-16k-dma3-lru-ram/software/bosio/include"
out=${1:?output directory};mode=${2:-host};mkdir -p "$out"
g++ -std=c++17 -O2 -I"$native" "$here/src/generate_geometry.cpp" "$native/geometry.cpp" -o "$out/generate"
"$out/generate" > "$out/tile_geometry.hpp"
python3 "$here/tools/generate_neighbors.py" "$native/fixed_geometry.inc" "$out/tile_neighbors.hpp"
g++ -std=c++17 -O2 -ffp-contract=off -I"$native" -I"$here/src" -I"$out" "$here/tests/check_projection.cpp" "$native/geometry.cpp" -o "$out/check-projection"
"$out/check-projection"
g++ -std=c++17 -O2 -ffp-contract=off -I"$native" -I"$uapi" -I"$out" "$here/tests/check_outline.cpp" "$native/geometry.cpp" -o "$out/check-outline"
"$out/check-outline"
compiler=g++;flags=(-std=c++17 -O2 -g -pthread -ffp-contract=off)
if [[ "$mode" == rv32 ]];then compiler=${RV32_CXX:-/var/tmp/haebancheon-userland-validation/buildroot/host/bin/riscv32-buildroot-linux-musl-g++};flags+=(-static -march=rv32ima_zicsr_zifencei -mabi=ilp32 -s);fi
if [[ "$mode" == sanitize ]];then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer);fi
"$compiler" "${flags[@]}" -Wall -Wextra -I"$native" -I"$uapi" -I"$out" "$here/src/triangle_wm.cpp" "$native/geometry.cpp" -o "$out/triangle-wm"
sha256sum "$out/triangle-wm" > "$out/triangle-wm.sha256"
"$compiler" "${flags[@]}" -Wall -Wextra -I"$native" -I"$uapi" -I"$out" "$here/src/boayo_service.cpp" "$native/geometry.cpp" -o "$out/triangle-boayo"
sha256sum "$out/triangle-boayo" > "$out/triangle-boayo.sha256"
"$compiler" "${flags[@]}" -Wall -Wextra -I"$native" "$here/src/boayo_app.cpp" -o "$out/triangle-app"
sha256sum "$out/triangle-app" > "$out/triangle-app.sha256"
cc=gcc;cflags=(-Os)
if [[ "$mode" == rv32 ]];then cc=${RV32_CC:-${compiler%g++}gcc};cflags+=(-static -s -march=rv32ima_zicsr_zifencei -mabi=ilp32);fi
"$cc" "${cflags[@]}" -Wall -Wextra "$here/tools/board_client.c" -o "$out/triangle-client"
sha256sum "$out/triangle-client" > "$out/triangle-client.sha256"
"$cc" "${cflags[@]}" -Wall -Wextra "$here/tools/apply_delta.c" -o "$out/triangle-apply-delta"
