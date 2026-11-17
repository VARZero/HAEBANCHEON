from pathlib import Path
import subprocess,json,sys
w=Path(sys.argv[1] if len(sys.argv)>1 else '/var/tmp/haebancheon-geometry-v13');v=Path(__file__).resolve().parents[1];s=Path(json.loads((w/'rv32/boayo-native-rv32.build.json').read_text())['source']);objs=[str(p) for p in (s.parent/'objects').glob('*.o') if p.name in ('geometry.o','wm.o')]
subprocess.run(['/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++','-std=c++17','-O2','-ffp-contract=off','-pthread','-static','-march=rv32ima_zicsr_zifencei','-mabi=ilp32','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-s','-I'+str(s/'src'),str(v/'tests/mask_benchmark.cpp'),*objs,'-o',str(w/'mask-bench-rv32')],check=True)
print('BENCH_BYTES', (w/'mask-bench-rv32').stat().st_size)

