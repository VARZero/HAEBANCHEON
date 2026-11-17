"""Build an independent native root; keep the Python reference and every existing SD intact."""
from pathlib import Path
import shutil,subprocess,hashlib,json,os,sys
here=Path(__file__).resolve().parent
out=Path(sys.argv[1] if len(sys.argv)>1 else '/var/tmp/oryukdo-boayo-native')
reference=Path('/var/tmp/oryukdo-boayo-boottrace-v1/root')
root=out/'root';root.mkdir(parents=True,exist_ok=True)
assert not any(root.iterdir()),'Use an empty native root; never overwrite a previous image root'
for source in sorted(reference.rglob('*')):
 relative=source.relative_to(reference);name=str(relative)
 if '__pycache__' in relative.parts or 'python' in name.lower() or name.startswith('opt/boayo') or name.startswith('bin/boayo-'):continue
 # All shipped executables are static. Avoid carrying the unused Python/C++ shared runtime.
 if relative.parts[0]=='lib' or name.startswith('usr/lib/lib'):continue
 dest=root/relative
 if source.is_symlink():dest.parent.mkdir(parents=True,exist_ok=True);dest.symlink_to(os.readlink(source))
 elif source.is_dir():dest.mkdir(parents=True,exist_ok=True)
 elif source.is_file():dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
binary=out/'boayo-native-rv32';assert binary.is_file()
build=json.loads(binary.with_suffix('.build.json').read_text())
assert hashlib.sha256(binary.read_bytes()).hexdigest()==build['binary_sha256']
for name,digest in build['source_sha256'].items():
 assert hashlib.sha256((here/name).read_bytes()).hexdigest()==digest,'Rebuild native executable after source edits: '+name
shutil.copy2(binary,root/'usr/bin/boayo-native')
subprocess.run(['/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip',str(root/'usr/bin/boayo-native')],check=True)
os.chmod(root/'usr/bin/boayo-native',0o755)
for p in root.rglob('*'):
 if p.is_file() and not p.is_symlink() and p.read_bytes()[:4]==b'\x7fELF':
  dynamic=subprocess.check_output(['riscv64-linux-gnu-readelf','-d',str(p)],text=True)
  program=subprocess.check_output(['riscv64-linux-gnu-readelf','-l',str(p)],text=True)
  assert '(NEEDED)' not in dynamic and 'INTERP' not in program, 'Shared runtime required: '+str(p)
for command in ('start','stop','status','move','click','app','selftest','dual-selftest','keys','view','center-click','launcher'):
 p=root/'bin'/('boayo-'+command);p.write_text('#!/bin/sh\nexec /usr/bin/boayo-native '+command+' "$@"\n');p.chmod(0o755)
(root/'etc/oryukdo-image').write_text('ORYUKDO-KFB-BoAYO-native-v2\nC++17 / BusyBox preserved / BOSIO ABI3 / board test pending\n')
licenses=root/'usr/share/boayo-native';licenses.mkdir(parents=True,exist_ok=True)
for source in (here/'reference/BoAYO-LICENSE',here/'reference/BOSIO-WM-LICENSE',here/'vendor/json-LICENSE.MIT'):shutil.copy2(source,licenses/source.name)
preserved={}
for name in ('init','bin/busybox','bin/ddrcheck','bin/bosioctl'):
 src=reference/name;dst=root/name
 assert src.read_bytes()==dst.read_bytes(),name
 preserved[name]=hashlib.sha256(dst.read_bytes()).hexdigest()
files={str(p.relative_to(root)):p.stat().st_size for p in root.rglob('*') if p.is_file() and not p.is_symlink()}
assert not any('python' in str(p.relative_to(root)).lower() or 'numpy' in str(p.relative_to(root)).lower() or '/PIL/' in str(p.relative_to(root)) for p in root.rglob('*'))
source_hashes={str(p.relative_to(here)):hashlib.sha256(p.read_bytes()).hexdigest() for sub in ('src','vendor') for p in sorted((here/sub).rglob('*')) if p.is_file()}
report=dict(image_id='ORYUKDO-KFB-BoAYO-native-v2',root_regular_bytes=sum(files.values()),native_binary_bytes=(root/'usr/bin/boayo-native').stat().st_size,files=files,preserved=preserved,source_sha256=source_hashes,reference_manifest_sha256=hashlib.sha256((here/'reference.sha256.json').read_bytes()).hexdigest(),hardware_tested=False)
(out/'root_audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('files','source_sha256','preserved')}))
