"""Audit built sources, stripped executable, embedded initramfs and SD payloads."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,tempfile,zlib

here=Path(__file__).resolve().parent.parent
repo=here.parent.parent
build=Path(sys.argv[1])
base=Path(sys.argv[2])
out=Path(sys.argv[3]);out.parent.mkdir(parents=True,exist_ok=True)
checks=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def same(a,b):
    assert a.read_bytes()==b.read_bytes(),(str(a),str(b))
def cpio_files(data):
    pos=0;files={}
    while data[pos:pos+6]==b'070701':
        fields=[int(data[pos+6+i*8:pos+14+i*8],16) for i in range(13)]
        size,namesize=fields[6],fields[11]
        name=data[pos+110:pos+110+namesize-1].decode().lstrip('./')
        pos=(pos+110+namesize+3)&~3
        if name=='TRAILER!!!':return files
        files[name]=data[pos:pos+size];pos=(pos+size+3)&~3
    raise AssertionError('invalid newc archive')

for component in ('driver','common','include'):
    for p in (here/'software/bosio'/component).rglob('*'):
        if p.is_file():same(p,build/'linux/drivers/misc/oryukdo-bosio'/p.relative_to(here/'software/bosio'))
checks.append('BOSIO built kernel sources match overlay')
same(repo/'updates/sd4-linux-v2/oryukdo_sd.c',build/'linux/drivers/mmc/host/oryukdo_sd.c')
same(repo/'variants/sd4-dma/software/storage/oryukdo_dma.c',build/'linux/drivers/dma/oryukdo_dma.c')
checks.append('SD v2 and original DMAengine driver unchanged')
manifest=json.loads((build/'apps/boayo-native-rv32.build.json').read_text())
for name,digest in manifest['source_sha256'].items():
    assert sha(here/'software/boayo/native'/name)==digest,name
    assert sha(Path(manifest['source'])/name)==digest,name
assert sha(here/'software/bosio/include/bosio_uapi.h')==manifest['uapi_sha256']
assert sha(build/'apps/boayo-native-rv32')==manifest['binary_sha256']
with tempfile.TemporaryDirectory() as temp:
    stripped=Path(temp)/'app'
    subprocess.run(['/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip',
                    '-o',str(stripped),str(build/'apps/boayo-native-rv32')],check=True)
    same(stripped,build/'root/usr/bin/boayo-native')
checks.append('RV32 build snapshot, UAPI and installed stripped app match')
archive=(build/'kernel/usr/initramfs_data.cpio').read_bytes()
kernel=(build/'firmware/Image').read_bytes()
assert archive in kernel,'initramfs not embedded in kernel'
files=cpio_files(archive)
for name in ('usr/bin/boayo-native','bin/bosio-dma-check','bin/bosio-rgb-check','bin/boayo-start','bin/storage-check','bin/busybox','init','init.console','etc/oryukdo-image'):
    assert files[name]==(build/'root'/name).read_bytes(),name
assert b'ORYUKDO-BoAYO-tiles-v5' in files['etc/oryukdo-image']
assert b'BOAYO_INIT_TRACE' in files['bin/boayo-start'] and b'BOAYO_INIT_TRACE_UART' in files['bin/boayo-start']
same(build/'root/bin/busybox',base/'root/bin/busybox')
same(build/'firmware/fw_jump.bin',base/'firmware/fw_jump.bin')
same(build/'firmware/gen3.dts',here/'software/bosio/gen3-bosio.dts')
assert 'dmas = <&storage_dma 0>;' in (build/'firmware/gen3.dts').read_text()
with tempfile.TemporaryDirectory() as temp:
    dtb=Path(temp)/'dtb'
    subprocess.run(['dtc','-I','dts','-O','dtb','-o',str(dtb),str(build/'firmware/gen3.dts')],check=True)
    same(dtb,build/'image/boot/gen3.dtb')
checks.append('embedded initramfs exact, BusyBox/OpenSBI preserved, DT DMA binding compiled')
assets=build/'data-root/boayo-assets'
am=json.loads((assets/'manifest.json').read_text())
assert len(am['assets'])==132 and am['generator_binary_sha256']==sha(build.parent/'host/boayo-native-host')
assert files['usr/share/boayo/assets/'+am['bootstrap']]==(assets/am['bootstrap']).read_bytes()
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp);commands=[]
    for a in am['assets']:
        assert sha(assets/a['file'])==a['sha256']
        commands.append('dump /boayo-assets/'+a['file']+' '+str(temp/a['file']))
    script=temp/'dump.txt';script.write_text('\n'.join(commands)+'\n')
    subprocess.run(['debugfs','-f',str(script),str(build/'image/data.ext4')],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
    for a in am['assets']:same(temp/a['file'],assets/a['file'])
checks.append('132 prebuilt assets exact in ext4; first launcher embedded in actual kernel; generator provenance matches')
plans=build/'data-root/boayo-plans'
pm=json.loads((plans/'manifest.json').read_text())
assert pm['generator_binary_sha256']==sha(build.parent/'host/boayo-native-host')
assert len(pm['plans'])==1
with tempfile.TemporaryDirectory() as temp:
    for entry in pm['plans']:
        p=plans/entry['file'];assert sha(p)==entry['sha256']
        assert files['usr/share/boayo/plans/'+p.name]==p.read_bytes()
        ready=plans/entry['ready_file']
        assert sha(ready)==entry['ready_sha256']
        assert files['usr/share/boayo/plans/'+ready.name]==ready.read_bytes()
        rt=Path(temp)/ready.name
        subprocess.run(['debugfs','-R','dump /boayo-plans/'+ready.name+' '+str(rt),str(build/'image/data.ext4')],check=True,stderr=subprocess.PIPE)
        same(ready,rt)
        target=Path(temp)/p.name
        subprocess.run(['debugfs','-R','dump /boayo-plans/'+p.name+' '+str(target),str(build/'image/data.ext4')],check=True,stderr=subprocess.PIPE)
        same(p,target)
        revision=json.loads(entry['key'])['revision']
        assert revision in (here/'software/boayo/native/src/plan_revision.hpp').read_text()
assert 'oryukdo,bosio-reserved-arena' in (build/'firmware/gen3.dts').read_text()
driver=(here/'software/bosio/driver/bosio_kfb.c').read_text()
assert 'd->arena=memremap' in driver and 'dma_alloc_coherent(dev,BO_ARENA_BYTES' not in driver
checks.append('plan exact in initramfs and ext4; generated from audited host/source revision; reserved DDR mapping without blanket zero; tile blend kernel interface built')
image=build/'image/ORYUKDO-KFB-BoAYO-tiles-v5.img'
data=image.read_bytes();header=2048*512;pos=header+512
assert data[510:512]==b'\x55\xaa' and data[header:header+8]==b'ORYSD001'
for i,name in enumerate(('fw_jump.bin','Image','gen3.dtb')):
    count,crc=struct.unpack_from('<II',data,header+8+i*8)
    payload=data[pos:pos+count*512];pos+=count*512
    assert zlib.crc32(payload)==crc,name
    expected=(build/('image/boot' if name=='gen3.dtb' else 'firmware')/name).read_bytes()
    assert payload==expected+b'\0'*(len(payload)-len(expected)),name
p1=struct.unpack_from('<B3sB3sII',data,446)
p2=struct.unpack_from('<B3sB3sII',data,462)
im=json.loads((build/'image/manifest.json').read_text())
assert p1[2]==0xda and p1[4]==2048 and p2[2]==0x83
assert pos<=(p1[4]+p1[5])*512<=p2[4]*512
assert p2[4]%2048==0 and p2[4]==im['data_lba'] and p2[5]==im['data_sectors']
assert (p2[4]+p2[5])*512==len(data)
assert data[p2[4]*512:]==(build/'image/data.ext4').read_bytes()
subprocess.run(['e2fsck','-fn',str(build/'image/data.ext4')],check=True)
checks.append('SD header CRCs and exact firmware/kernel/DTB/ext4 payloads; ext4 fsck')
report=dict(result='PASS_ARTIFACT_AUDIT',hardware=False,checks=checks,
 image_sha256=sha(image),image_bytes=image.stat().st_size,
 kernel_sha256=sha(build/'firmware/Image'),app_sha256=sha(build/'root/usr/bin/boayo-native'),
 busybox_sha256=sha(build/'root/bin/busybox'),driver_sha256=sha(here/'software/bosio/driver/bosio_kfb.c'),
 app_build_manifest=manifest)
out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
