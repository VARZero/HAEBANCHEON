from pathlib import Path
import subprocess
r=Path('/var/tmp/haebancheon-view-v8')
subprocess.run(['python3',str(r/'tools/make_view_path.py'),str(r/'view-path.bin')],check=True)
objs=[str(p) for p in (r/'host3').rglob('*.o') if p.name!='main.o']
subprocess.run(['g++','-std=c++17','-O2','-pthread','-I'+str(r/'software/boayo/native/src'),str(r/'tests/test_view.cpp'),*objs,'-o',str(r/'test_view')],check=True)
subprocess.run([str(r/'test_view'),str(r/'view-path.bin')],check=True)
subprocess.run(['python3',str(r/'tests/test_view_service.py'),str(r/'host3/boayo-native-host'),str(r/'view-path.bin')],check=True)
