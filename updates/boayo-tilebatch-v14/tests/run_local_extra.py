from pathlib import Path
import json,subprocess,sys
v=Path(__file__).resolve().parents[1];work=Path(sys.argv[1]);reports=work/'extra';reports.mkdir(exist_ok=True)
for mode in ['reference','host']:
 binary=work/mode/'boayo-native-host';src=Path(json.loads(Path(str(binary)+'.build.json').read_text())['source']);objects=[str(p) for p in (src.parent/'objects').glob('*.o') if p.name!='main.o']
 for test in (['test_tile_reuse'] if mode=='reference' else ['test_tile_reuse','test_damage','corner_work']):
  target=reports/(mode+'-'+test)
  subprocess.run(['g++','-std=c++17','-O2','-ffp-contract=off','-pthread','-I'+str(src/'src'),str(v/'tests'/(test+'.cpp')),*objects,'-o',str(target)],check=True)
  with target.with_suffix('.log').open('w') as f:subprocess.run([str(target)],stdout=f,stderr=subprocess.STDOUT,check=True)
  print(target.with_suffix('.log').read_text(),flush=True)
a=json.loads((reports/'reference-test_tile_reuse.log').read_text());b=json.loads((reports/'host-test_tile_reuse.log').read_text());assert b['warm_upload_bytes']==b['warm_upload_calls']==0;assert b['warm_batch_calls']<a['warm_batch_calls'];assert b['diagonal_upload_bytes']<a['diagonal_upload_bytes']
(reports/'comparison.json').write_text(json.dumps(dict(result='PASS_PIXELS_AND_REDUCED_TRAFFIC',reference=a,candidate=b,physical_timing=False),indent=2)+'\n')
