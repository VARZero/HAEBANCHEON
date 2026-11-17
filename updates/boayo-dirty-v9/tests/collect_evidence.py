"""Collect already completed checks; does not alter the board or source build."""
from pathlib import Path
import hashlib,json,shutil,sys
here=Path(__file__).resolve().parent.parent
work=Path(sys.argv[1]); delivery=Path(sys.argv[2]); evidence=here/'evidence'
evidence.mkdir(exist_ok=True);delivery.mkdir(parents=True,exist_ok=True)
shutil.copytree(work/'reports',evidence,dirs_exist_ok=True)
for name in ('build-host3.log','build-sanitize.log','build-rv32-final.log','build-image.log','resident.log','motion-pixels.log','no-assets.log','app-traffic.log','view-service.log','qemu.log'):
 shutil.copy2(work/name,evidence/name)
for folder,mode in [('host3','host'),('sanitize','sanitize'),('rv32-final','rv32')]:
 shutil.copy2(work/folder/f'boayo-native-{mode}.build.json',evidence/f'build-{mode}.json')
shutil.copy2(work/'release/image/manifest.json',evidence/'image-manifest.json')
checks={}
for folder in ('resident','motion-pixels','no-assets','app-traffic','qemu'):
 data=json.loads((evidence/folder/'result.json').read_text());assert data['result'].startswith('PASS'),data
 checks[folder]=data['result']
audit=json.loads((evidence/'artifact-audit.json').read_text());assert audit['result']=='PASS_ARTIFACT_AUDIT'
image=delivery/'HAEBANCHEON-KFB-BoAYO-dirty-v9.img'
assert hashlib.file_digest(image.open('rb'),'sha256').hexdigest()==audit['image_sha256']
for mode in ('host3','sanitize'):
 assert 'PASS dirty texture pool' in (evidence/f'pool-{mode}.log').read_text()
summary=dict(result='PASS_LOCAL_ONLY',hardware_tested=False,rtl_changed=False,driver_changed=False,scene_comparisons=107,checks=checks,image_sha256=audit['image_sha256'],image_bytes=image.stat().st_size,limitations=['No physical-board or HDMI timing measurement','QEMU uses extracted SD payloads; it does not model the SD controller or BOSIO hardware','First image load still required; 4MiB texture pool; existing compose rate cap retained'])
(evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
shutil.copytree(evidence,delivery/'evidence',dirs_exist_ok=True)
shutil.copy2(here/'README_KO.md',delivery/'README_KO.md')
print(json.dumps(summary,indent=2))
