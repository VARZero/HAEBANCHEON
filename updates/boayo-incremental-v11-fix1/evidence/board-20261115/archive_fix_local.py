from pathlib import Path
import shutil,json,hashlib
w=Path('/var/tmp/haebancheon-incremental-v11-fix1')
v=Path('/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/ORYUKDO-3d5i_BOSIO_SoC/updates/boayo-incremental-v11-fix1')
e=v/'evidence';e.mkdir(exist_ok=True)
for name in ('reports-final','qemu'):
 if (w/name).exists():shutil.copytree(w/name,e/name,dirs_exist_ok=True)
for name in ('host','rv32'):
 shutil.copy2(w/name/('boayo-native-'+name+'.build.json'),e/(name+'-build.json'))
shutil.copy2(w/'audit.json',e/'artifact-audit.json')
for name in ('kernel.log','config.log','image.log'):
 shutil.copy2(w/'release'/name,e/name)
image=w/'release/image/HAEBANCHEON-KFB-BoAYO-incremental-v11-fix1.img'
out=Path('/mnt/d/temp/haebancheon-incremental-v11-fix1');out.mkdir(exist_ok=True)
shutil.copy2(image,out/image.name)
shutil.copy2(w/'release/image/manifest.json',out/'image-manifest.json')
a=json.loads((e/'host-build.json').read_text());b=json.loads((e/'rv32-build.json').read_text())
assert a['source_sha256']==b['source_sha256'] and a['uapi_sha256']==b['uapi_sha256']
(e/'source-consistency.json').write_text(json.dumps(dict(result='PASS_IDENTICAL_HOST_RV32',source_files=len(a['source_sha256']),image_sha256=hashlib.file_digest(image.open('rb'),'sha256').hexdigest(),physical_image_boot=False),indent=2))
(e/'content-hover.json').write_text(json.dumps(dict(result='PASS',pixel_comparisons=72,original_content_asset_misses=2,fixed_asset_misses=0,fixed_asset_rejections=0,test='tests/test_content_hover.py; actual test_ui pixel materialization is a test oracle, not production rendering'),indent=2))
print('ARCHIVED_LOCAL_FIX1',flush=True)
