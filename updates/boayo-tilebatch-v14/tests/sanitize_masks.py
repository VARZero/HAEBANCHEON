from pathlib import Path
import json,subprocess
here=Path(__file__).resolve().parent;w=Path('/var/tmp/haebancheon-tilebatch-v14')
m=json.loads((w/'host/boayo-native-host.build.json').read_text());s=Path(m['source'])
objects=[str(p) for p in (s.parent/'objects').glob('*.o') if p.name!='main.o']
subprocess.run(['g++','-std=c++17','-O1','-g','-ffp-contract=off','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(s/'src'),str(here/'test_masks.cpp'),*objects,'-o',str(w/'sanitize_masks')],check=True)
with (w/'mask-sanitize.log').open('w') as f:subprocess.run([str(w/'sanitize_masks')],stdout=f,stderr=subprocess.STDOUT,check=True)
print((w/'mask-sanitize.log').read_text())
