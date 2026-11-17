from pathlib import Path
import json,subprocess,sys
here=Path(__file__).resolve().parent;work=Path('/var/tmp/haebancheon-tilebatch-v14')
binary=work/'host/boayo-native-host'
m=json.loads(Path(str(binary)+'.build.json').read_text());source=Path(m['source'])
objects=list((source.parent/'objects').glob('*.o'));objects=[str(p) for p in objects if p.name!='main.o']
cmd=['g++','-std=c++17','-O2','-g','-ffp-contract=off','-pthread','-I'+str(source/'src'),str(here/'test_masks.cpp'),*objects,'-o',str(work/'test_masks')]
subprocess.run(cmd,check=True)
with (work/'mask-proof.log').open('w') as log:subprocess.run([str(work/'test_masks')],stdout=log,stderr=subprocess.STDOUT,check=True)
print((work/'mask-proof.log').read_text(),flush=True)
cmd[cmd.index(str(here/'test_masks.cpp'))]=str(here/'test_culling.cpp');cmd[-1]=str(work/'test_culling')
subprocess.run(cmd,check=True)
with (work/'culling-proof.log').open('w') as log:subprocess.run([str(work/'test_culling')],stdout=log,stderr=subprocess.STDOUT,check=True)
print((work/'culling-proof.log').read_text(),flush=True)
subprocess.run(['python3',str(here/'prove_plans.py'),str(binary),'/var/tmp/haebancheon-motion-v7/release/data-root/boayo-plans',str(here.parent/'software/boayo/native'),str(work/'plan-proof.json')],check=True)
