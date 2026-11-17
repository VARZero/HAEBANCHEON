from pathlib import Path
import hashlib,json,shutil,struct,sys,zlib
assets=Path(sys.argv[1]);root=Path(sys.argv[2]);manifest=json.loads((assets/'manifest.json').read_text())
# Full validation happens once on the build PC, never in the board frame loop.
assert manifest['format']=='BUITEX01'
seen=set()
for entry in manifest['assets']:
 name=entry['file'];assert Path(name).name==name and name not in seen
 seen.add(name);data=(assets/name).read_bytes()
 assert len(data)==entry['bytes'] and hashlib.sha256(data).hexdigest()==entry['sha256'],name
 magic,w,h,k,n,crc,hcrc=struct.unpack_from('<8sIIIIII',data)
 key=json.dumps(entry['key'],sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()
 assert magic==b'BUITEX01' and w==entry['width'] and h==entry['height'] and w>0 and h>0,name
 assert k==len(key) and k<=4064 and data[32:32+k]==key,name
 assert n==w*h*4 and len(data)==4096+n,name
 assert zlib.crc32(data[4096:])==crc and zlib.crc32(data[:28]+key)==hcrc,name
 hashed=2166136261
 for byte in key:hashed=((hashed^byte)*16777619)&0xffffffff
 assert name==f'{hashed:08x}.bui',name
assert manifest['bootstrap'] in seen
dest=root/'usr/share/boayo/assets';dest.mkdir(parents=True,exist_ok=True)
# The first launcher must work even without /data (also exercises the exact
# release kernel asset path under QEMU). Full catalog also lives on SD ext4.
shutil.copy2(assets/manifest['bootstrap'],dest/manifest['bootstrap'])
(dest/'bootstrap.json').write_text(json.dumps(dict(file=manifest['bootstrap'],format=manifest['format']))+'\n')

# Critical app recipes and shared captions are available before /data is mounted.
for entry in manifest['assets']:
 if entry['key'][0] in ('app-base','app-digit','app-strip','caption'):
  shutil.copy2(assets/entry['file'],dest/entry['file'])
print('PASS BUILD_ASSET_GATE',len(seen),'assets; SHA, header/key, dimensions and payload CRC')
