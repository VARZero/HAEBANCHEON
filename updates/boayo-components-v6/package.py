"""Publish locally only after exact-image/source validation passes."""
from pathlib import Path
import hashlib,json,shutil,sys,zipfile
here=Path(__file__).resolve().parent
work,dest=map(Path,sys.argv[1:3]);reports=work/'reports'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(name):return json.loads((reports/name).read_text())
audit=read('artifact.json');qemu=read('qemu/result.json');suite=read('suite/suite.json')
assert audit['result']=='PASS_ARTIFACT_AUDIT' and qemu['result']=='PASS_QEMU_ONLY'
assert qemu['image_sha256']==audit['image_sha256']
assert suite['result']=='PASS' and len(suite['tests'])==9
assert suite['binary_sha256']==sha(work/'host/boayo-native-host')
for name,binary in [('components','host/boayo-native-host'),('components-sanitize','sanitize/boayo-native-sanitize')]:
 r=read(name+'/result.json');assert r['result']=='PASS'
 assert r['candidate_sha256']==sha(work/binary)
for name,phrase in [('component-rtl.log','PASS_COMPONENT_SCENES'),('sd-rtl.log','PASS boot-to-runtime ownership'),('build-asset-gate.log','PASS BUILD_ASSET_GATE'),('build-plan-gate.log','PASS BUILD_PLAN_GATE')]:
 assert phrase in (reports/name).read_text(),name
manifests=[json.loads((work/p).read_text()) for p in ('host/boayo-native-host.build.json','apps/boayo-native-rv32.build.json','sanitize/boayo-native-sanitize.build.json')]
assert all(m['source_sha256']==manifests[0]['source_sha256'] for m in manifests)
image=work/'release/image/HAEBANCHEON-KFB-BoAYO-components-v6.img'
assert sha(image)==audit['image_sha256']
dest.mkdir(parents=True,exist_ok=True)
shutil.copy2(image,dest/image.name);assert sha(dest/image.name)==sha(image)
shutil.copy2(work/'release/image/manifest.json',dest/'manifest.json')
for name in ('README_KO.md','검증_변경기록.md','asset_catalog.json'):shutil.copy2(here/name,dest/name)
shutil.copytree(reports,dest/'reports',dirs_exist_ok=True,ignore=shutil.ignore_patterns('*.hex'))
evidence=here/'evidence';evidence.mkdir(exist_ok=True)
for name in ('artifact.json','component-rtl.log','sd-rtl.log','sd-rtl-sources.txt','build-asset-gate.log','build-plan-gate.log','sd-rtl-initial-watchdog.log'):
 shutil.copy2(reports/name,evidence/name)
for name in ('components','components-sanitize','suite','qemu'):
 shutil.copytree(reports/name,evidence/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('*.hex'))
with zipfile.ZipFile(dest/'HAEBANCHEON-BoAYO-components-v6-source.zip','w',zipfile.ZIP_DEFLATED) as archive:
 for p in sorted(here.rglob('*')):
  if p.is_file() and '__pycache__' not in p.parts:archive.write(p,p.relative_to(here.parent))
(dest/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in (dest/image.name,dest/'HAEBANCHEON-BoAYO-components-v6-source.zip')))
print('PASS_LOCAL_PACKAGE',dest,sha(dest/image.name))
