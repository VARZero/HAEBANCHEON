from pathlib import Path
import json,subprocess,sys
v=Path(__file__).resolve().parents[1];w=Path(sys.argv[1]);binary=w/'sanitize/boayo-native-sanitize';source=Path(json.loads(Path(str(binary)+'.build.json').read_text())['source']);objs=[str(p) for p in (source.parent/'objects').glob('*.o') if p.name!='main.o'];out=w/'sanitizer-reports';out.mkdir(exist_ok=True)
for test in ['test_damage','test_asset_lifetime']:
 target=out/test
 subprocess.run(['g++','-std=c++17','-O2','-g','-ffp-contract=off','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(source/'src'),str(v/'tests'/(test+'.cpp')),*objs,'-o',str(target)],check=True)
 with (out/(test+'.log')).open('w') as f:subprocess.run([str(target)],stdout=f,stderr=subprocess.STDOUT,check=True)
 print((out/(test+'.log')).read_text(),flush=True)
