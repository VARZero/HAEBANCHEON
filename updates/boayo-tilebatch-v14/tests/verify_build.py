"""Audit final executable, embedded assets, SD payloads and L1/DMA3 provenance."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,tempfile,zlib
here=Path(__file__).resolve().parent.parent;repo=here.parent.parent
build,base,out=map(Path,sys.argv[1:4]);work=build.parent
variant=repo/'variants/l1-16k-dma3-lru-ram';checks=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def same(a,b):assert a.read_bytes()==b.read_bytes(),(str(a),str(b))
def cpio_files(data):
 pos=0;files={}
 while data[pos:pos+6]==b'070701':
  fields=[int(data[pos+6+i*8:pos+14+i*8],16) for i in range(13)]
  size,namesize=fields[6],fields[11]
  name=data[pos+110:pos+110+namesize-1].decode().lstrip('./');pos=(pos+110+namesize+3)&~3
  if name=='TRAILER!!!':return files
  files[name]=data[pos:pos+size];pos=(pos+size+3)&~3
 raise AssertionError('invalid newc archive')
manifest=json.loads((work/'rv32/boayo-native-rv32.build.json').read_text())
for name,digest in manifest['source_sha256'].items():
 assert sha(here/'software/boayo/native'/name)==digest,name
 assert sha(Path(manifest['source'])/name)==digest,name
assert sha(here/'software/bosio/include/bosio_uapi.h')==manifest['uapi_sha256']
assert sha(work/'rv32/boayo-native-rv32')==manifest['binary_sha256']
with tempfile.TemporaryDirectory() as temp:
 stripped=Path(temp)/'app'
 subprocess.run(['/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip','-o',str(stripped),str(work/'rv32/boayo-native-rv32')],check=True)
 same(stripped,build/'root/usr/bin/boayo-native')
checks.append('RV32 final sources, snapshot, UAPI and installed stripped app match')
archive=(build/'kernel/usr/initramfs_data.cpio').read_bytes();kernel=(build/'firmware/Image').read_bytes()
assert archive in kernel;files=cpio_files(archive)
for name in ('usr/bin/boayo-native','bin/bosio-dma-check','bin/boayo-start','bin/storage-check','bin/busybox','init','init.console','etc/oryukdo-image'):
 assert files[name]==(build/'root'/name).read_bytes(),name
assert b'HAEBANCHEON-BoAYO-tilebatch-v14' in files['etc/oryukdo-image']
same(build/'root/bin/busybox',base/'root/bin/busybox');same(build/'firmware/fw_jump.bin',base/'firmware/fw_jump.bin')
same(build/'firmware/gen3.dts',variant/'software/bosio/gen3-bosio.dts')
assert 'dma-coherent;' in (build/'firmware/gen3.dts').read_text()
assert (build/'kernel/source').resolve()==(build/'linux').resolve()
driver=build/'linux/drivers/misc/oryukdo-bosio/driver/bosio_kfb.c'
same(driver,here/'software/bosio/driver/bosio_kfb.c')
for folder in ('driver','common','include'):
 for p in (here/'software/bosio'/folder).glob('*'):
  if p.is_file():same(p,build/'linux/drivers/misc/oryukdo-bosio'/folder/p.name)
assert 'd->arena=memremap' in driver.read_text()
checks.append('exact initramfs; BusyBox/OpenSBI preserved; isolated L1 DMA3 tree with exact v11 driver/common/UAPI, coherent DT')
assets=build/'data-root/boayo-assets';plans=build/'data-root/boayo-plans'
am=json.loads((assets/'manifest.json').read_text());pm=json.loads((plans/'manifest.json').read_text())
for folder in ('boayo-assets','boayo-plans'):
 same(build/'data-root'/folder/'manifest.json',work/('assets' if folder=='boayo-assets' else 'plans')/'manifest.json')
assert len(am['assets'])==689 and len(pm['plans'])==37
to_dump=[]
for entry in am['assets']:
 p=assets/entry['file'];assert sha(p)==entry['sha256'];to_dump.append(('boayo-assets/'+p.name,p))
 if entry['key'][0] in ('app-base','app-digit','app-strip','caption') or p.name==am['bootstrap']:
  assert files['usr/share/boayo/assets/'+p.name]==p.read_bytes(),p.name
for entry in pm['plans']:
 for key,digest in [('file','sha256'),('ready_file','ready_sha256')]:
  if not entry.get(key):continue
  p=plans/entry[key];assert sha(p)==entry[digest]
  assert files['usr/share/boayo/plans/'+p.name]==p.read_bytes(),p.name
  to_dump.append(('boayo-plans/'+p.name,p))
with tempfile.TemporaryDirectory() as temp:
 temp=Path(temp);script=temp/'dump.txt'
 script.write_text('\n'.join(f'dump /{name} {temp/str(i)}' for i,(name,p) in enumerate(to_dump))+'\n')
 subprocess.run(['debugfs','-f',str(script),str(build/'image/data.ext4')],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
 for i,(name,p) in enumerate(to_dump):same(temp/str(i),p)
checks.append('689 assets and 37 plans exact in ext4; shared controls/app recipes/all plans exact in kernel')
image=build/'image/HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img'
data=image.read_bytes();header=2048*512;pos=header+512
assert data[510:512]==b'\x55\xaa' and data[header:header+8]==b'ORYSD001'
for i,name in enumerate(('fw_jump.bin','Image','gen3.dtb')):
 count,crc=struct.unpack_from('<II',data,header+8+i*8);payload=data[pos:pos+count*512];pos+=count*512
 expected=(build/('image/boot' if name=='gen3.dtb' else 'firmware')/name).read_bytes()
 assert zlib.crc32(payload)==crc and payload==expected+b'\0'*(len(payload)-len(expected)),name
assert 0x80400000+len(kernel)<0x87f00000,'kernel overlaps DT'
p1=struct.unpack_from('<B3sB3sII',data,446);p2=struct.unpack_from('<B3sB3sII',data,462)
im=json.loads((build/'image/manifest.json').read_text())
assert p1[2]==0xda and p1[4]==2048 and p2[2]==0x83
assert pos<=(p1[4]+p1[5])*512<=p2[4]*512
assert p2[4]%2048==0 and p2[4]==im['data_lba'] and (p2[4]+p2[5])*512==len(data)
assert data[p2[4]*512:]==(build/'image/data.ext4').read_bytes() and sha(image)==im['sha256']
subprocess.run(['e2fsck','-fn',str(build/'image/data.ext4')],check=True)
checks.append('SD CRCs, exact payloads/ext4, partitions and kernel/DT nonoverlap, fsck')
report=dict(result='PASS_ARTIFACT_AUDIT',hardware=False,checks=checks,image_sha256=sha(image),image_bytes=len(data),kernel_bytes=len(kernel),kernel_sha256=sha(build/'firmware/Image'),app_sha256=sha(build/'root/usr/bin/boayo-native'),driver_sha256=sha(driver),app_build_manifest=manifest)
out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
