#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd);cd "$repo"
out=${1:-/var/tmp/triangle-matrix};mode=${2:-rv32}
native=$repo/updates/boayo-tilebatch-v14/software/boayo/native/src
uapi=$repo/variants/l1-16k-dma3-lru-ram/software/bosio/include
mkdir -p "$out/float" "$out/fixed"
cp updates/triangle-wm/src/* "$out/float/"
python3 updates/triangle-wm/tools/prepare_fixed.py "$out/fixed"
# Driver is held constant across every cell of the experiment.
python3 - "$out/float" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'content_render.inc';p.write_text(p.read_text().replace('call(BO_PUBLISH,&p);','call(BO_PUBLISH_COPY,&p);'))
p=Path(sys.argv[1])/'move_marker.inc';p.write_text(p.read_text().replace('call(BO_PUBLISH,&p);','call(BO_PUBLISH_COPY,&p);'))
p=Path(sys.argv[1])/'triangle_wm.cpp';p.write_text(p.read_text().replace('else if(op==BO_PUBLISH)', 'else if(op==BO_PUBLISH||op==BO_PUBLISH_COPY)'))
# Stop per-update demo formatting in BOTH modes, matching the shipping fixed app.
p=Path(sys.argv[1])/'boayo_app.cpp';p.write_text(p.read_text().replace('if(d.w) measure(app,"demo_update",[&]{return surface.commit(app,d);});','if(d.w)wait(app,surface.commit(app,d));'))
PY
tables=/var/tmp/triangle-fixed-release
compiler=g++;flags=(-std=c++17 -O2 -g -pthread -ffp-contract=off -Werror=return-type)
if [[ "$mode" == rv32 ]];then compiler=/var/tmp/haebancheon-userland-validation/buildroot/host/bin/riscv32-buildroot-linux-musl-g++;flags+=(-static -march=rv32ima_zicsr_zifencei -mabi=ilp32);fi
if [[ "$mode" == sanitize ]];then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer);fi
inc=(-I"$native" -I"$uapi" -I"$tables")
"$compiler" "${flags[@]}" "${inc[@]}" -Dmain=float_server_main -Dtriangle_service=triangle_float -DEngine=FloatEngine -DWindow=FloatWindow -DWindowTransform=FloatWindowTransform -DRenderStats=FloatRenderStats -DUiPreview=FloatUiPreview -c "$out/float/boayo_service.cpp" -o "$out/float.o"
"$compiler" "${flags[@]}" "${inc[@]}" -Dmain=fixed_server_main -c "$out/fixed/boayo_service.cpp" -o "$out/fixed.o"
"$compiler" "${flags[@]}" "${inc[@]}" -Dmain=matrix_app_main -c "$out/float/boayo_app.cpp" -o "$out/app.o"
"$compiler" "${flags[@]}" "${inc[@]}" -Dmain=matrix_input_main -c updates/triangle-wm/src/input_client.cpp -o "$out/input.o"
"$compiler" "${flags[@]}" "${inc[@]}" -c "$native/geometry.cpp" -o "$out/geometry.o"
"$compiler" "${flags[@]}" updates/triangle-wm/tests/matrix_main.cpp "$out/float.o" "$out/fixed.o" "$out/app.o" "$out/input.o" "$out/geometry.o" -o "$out/wm-matrix"
if [[ "$mode" == rv32 ]];then /var/tmp/haebancheon-userland-validation/buildroot/host/bin/riscv32-buildroot-linux-musl-strip "$out/wm-matrix";fi
echo MATRIX_BUILD_OK
