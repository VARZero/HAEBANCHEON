"""Run in WSL after building host and sanitize variants under the supplied root."""
from pathlib import Path
import subprocess,sys
r=Path(sys.argv[1]); reports=r/'reports';reports.mkdir(exist_ok=True)
for mode in ('host3','sanitize'):
    objects=sorted((r/mode).rglob('*.o'))
    objects=[p for p in objects if p.name!='main.o']
    assert objects
    src=next((r/mode).glob('source-*/software/boayo/native/src'))
    binary=r/f'test_texture_pool_{mode}'
    flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if mode=='sanitize' else []
    subprocess.run(['g++','-std=c++17','-O2','-g','-pthread',*flags,'-I'+str(src),str(r/'tests/test_texture_pool.cpp'),*map(str,objects),'-o',str(binary)],check=True)
    with (reports/f'pool-{mode}.log').open('w') as log:
        subprocess.run([str(binary)],stdout=log,stderr=subprocess.STDOUT,check=True)
    print((reports/f'pool-{mode}.log').read_text(),flush=True)
