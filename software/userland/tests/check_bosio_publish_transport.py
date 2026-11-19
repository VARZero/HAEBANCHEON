"""Check the real LinuxDevice publisher with an ioctl recording shim."""
from pathlib import Path
import json,subprocess,sys
manifest=Path(sys.argv[1]);build=json.loads(manifest.read_text());source=Path(build['source'])
objects=sorted(p for p in (source.parent/'objects').glob('*.o') if p.name!='main.o')
if len(objects)!=12:raise RuntimeError('expected native host build objects; run native/build.sh OUTPUT host first')
test=Path(__file__).with_name('bosio_publish_transport.cpp');binary=manifest.parent/'test_bosio_publish_transport'
subprocess.run(['g++','-std=c++17','-O2','-pthread','-I',str(source/'src'),str(test),*[str(p) for p in objects],'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
