from pathlib import Path
import subprocess,json
import sys
w=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/var/tmp/haebancheon-geometry-v13');v=Path(__file__).resolve().parents[1];s=Path(json.loads((w/'host/boayo-native-host.build.json').read_text())['source']);objs=[str(p) for p in (s.parent/'objects').glob('*.o') if p.name!='main.o']
subprocess.run(['g++','-std=c++17','-O2','-ffp-contract=off','-pthread','-I'+str(s/'src'),str(v/'tests/test_geometry.cpp'),*objs,'-o',str(w/'test_geometry')],check=True)
with (w/'geometry-proof.log').open('w') as f:subprocess.run([str(w/'test_geometry')],stdout=f,stderr=subprocess.STDOUT,check=True)
print((w/'geometry-proof.log').read_text())
subprocess.run(['python3',str(v/'tests/prove_plans.py'),str(w/'host/boayo-native-host'),'/var/tmp/haebancheon-motion-v7/release/data-root/boayo-plans',str(v/'software/boayo/native'),str(w/'plan-proof.json')],check=True)
