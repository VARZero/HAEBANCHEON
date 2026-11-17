from pathlib import Path
import json,subprocess
import sys
w=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/var/tmp/haebancheon-geometry-v13');v=Path(__file__).resolve().parents[1]
for mode in ('host','rv32'):
 s=Path(json.loads((w/mode/('boayo-native-'+mode+'.build.json')).read_text())['source']);objs=[str(p) for p in (s.parent/'objects').glob('*.o') if p.name in ('geometry.o','wm.o')]
 compiler='g++' if mode=='host' else '/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++'
 flags=['-std=c++17','-O2','-ffp-contract=off','-pthread','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
 if mode=='rv32':flags+=['-static','-march=rv32ima_zicsr_zifencei','-mabi=ilp32','-s']
 target=w/('geometry-bench-'+mode)
 subprocess.run([compiler,*flags,'-I'+str(s/'src'),str(v/'tests/geometry_benchmark.cpp'),*objs,'-o',str(target)],check=True)
 if mode=='host':
  with (w/'geometry-bench-host.log').open('w') as f:subprocess.run([str(target)],stdout=f,check=True)
 print('BUILT',mode,target.stat().st_size,flush=True)
