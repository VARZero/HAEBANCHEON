from pathlib import Path
import hashlib,json,sys
base,out=map(Path,sys.argv[1:3])
here=Path(__file__).resolve().parent
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(here/'oryukdo_sd.c')==sha(out/'linux/drivers/mmc/host/oryukdo_sd.c')
assert 'driver_sha256='+sha(here/'oryukdo_sd.c') in (out/'reports/driver_test.log').read_text()
old=out/'image/ORYUKDO-KFB-SD4-DMA-v1.img'
new=out/'image/ORYUKDO-KFB-SD4-DMA-v2.img'
if old.exists(): old.replace(new)
manifest=json.loads((out/'image/manifest.json').read_text())
assert sha(new)==manifest['sha256']
manifest['image']=str(new)
(out/'image/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
changed=[];same=0
for p in (base/'root').rglob('*'):
 rel=p.relative_to(base/'root');q=out/'root'/rel
 if p.is_symlink():
  assert q.is_symlink() and p.readlink()==q.readlink(),str(rel)
 elif p.is_file():
  assert q.is_file(),str(rel)
  if sha(p)!=sha(q):changed.append(str(rel))
  else:same+=1
assert set(changed)=={'init','etc/oryukdo-image'},changed
assert 'mount -t ext4 /dev/mmcblk0p2 /data' in (out/'root/init').read_text()
assert '-o noatime' not in (out/'root/init').read_text()
for name in ['fw_jump.bin','gen3.dts']:
 assert sha(out/'firmware'/name)==sha(base/'firmware'/name)
report={'result':'PASS_PACKAGE','driver_sha256':sha(here/'oryukdo_sd.c'),
 'image_sha256':sha(new),'bytes':new.stat().st_size,
 'preserved_root_regular_files':same,'changed_root_files':changed,
 'busybox_sha256':sha(out/'root/bin/busybox'),
 'firmware_dts_preserved':True,'bitstream_changed':False,
 'hardware_v2_tested':False}
(out/'reports/package.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
