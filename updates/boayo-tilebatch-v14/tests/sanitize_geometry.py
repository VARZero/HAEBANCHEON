from pathlib import Path
import subprocess,json
w=Path('/var/tmp/haebancheon-tilebatch-v14');v=Path('/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/ORYUKDO-3d5i_BOSIO_SoC/updates/boayo-tilebatch-v14');s=Path(json.loads((w/'host/boayo-native-host.build.json').read_text())['source']);flags=['-std=c++17','-O1','-g','-ffp-contract=off','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer']
obj=w/'geometry-sanitized.o';subprocess.run(['g++',*flags,'-c',str(s/'src/geometry.cpp'),'-o',str(obj)],check=True)
others=[str(p) for p in (s.parent/'objects').glob('*.o') if p.name not in ('main.o','geometry.o')]
subprocess.run(['g++',*flags,'-I'+str(s/'src'),str(v/'tests/test_geometry.cpp'),str(obj),*others,'-o',str(w/'test_geometry_sanitized')],check=True)
with (w/'geometry-sanitize.log').open('w') as f:subprocess.run([str(w/'test_geometry_sanitized')],stdout=f,stderr=subprocess.STDOUT,check=True)
print((w/'geometry-sanitize.log').read_text())
