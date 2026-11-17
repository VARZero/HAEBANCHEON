"""Validate every built-in plan before including it in immutable initramfs."""
from pathlib import Path
import hashlib,json,shutil,struct,sys,zlib
from ready_plan import device_plan
plans=Path(sys.argv[1]);root=Path(sys.argv[2]);m=json.loads((plans/'manifest.json').read_text())
for plan in m['plans']:
 data=(plans/plan['file']).read_bytes()
 assert hashlib.sha256(data).hexdigest()==plan['sha256']
 magic,n,size,k,crc,hcrc=struct.unpack_from('<8sIIIII',data)
 assert magic==b'BOGPLN02' and 0<n<=4220 and n*84<=size<=4220*(84+1152)
 assert size%4==0 and len(data)==4096+size and k<=4068
 key=data[28:28+k];assert key.decode()==plan['key']
 assert zlib.crc32(data[:24]+key)==hcrc and zlib.crc32(data[4096:])==crc
 geometry=json.loads(key)['geometry'];width,height=geometry[:2]
 body=data[4096:];cursor=n*84;previous=-1
 for i in range(n):
  record=struct.unpack_from('<21I',body,i*84);tile,kind,offset=record[:3];desc=record[3:]
  assert previous<tile<4220 and kind in (0,1,2);previous=tile
  assert desc[:6]==(0,tile*256,width,height,width,1)
  if kind==2:
   assert offset==cursor and cursor+1152<=size
   mask=body[cursor:cursor+1024];assert set(mask)=={0,1}
   packed=struct.unpack_from('<32I',body,cursor+1024)
   assert all(((packed[j//32]>>(j%32))&1)==mask[j] for j in range(1024))
   cursor+=1152
  else:assert offset==0
 assert cursor==size
 ready=(plans/plan['ready_file']).read_bytes()
 assert hashlib.sha256(ready).hexdigest()==plan['ready_sha256']
 assert ready==device_plan(data), 'device recipe mismatch'
 print('PASS_BUILD_PLAN',plan['file'],n,size)
dest=root/'usr/share/boayo/plans';dest.mkdir(parents=True,exist_ok=True)
for old in dest.glob('p_*.bgp'):old.unlink()
shutil.copy2(plans/m['bootstrap'],dest/m['bootstrap'])

shutil.copy2(plans/(m['bootstrap']+'.ready'),dest/(m['bootstrap']+'.ready'))
