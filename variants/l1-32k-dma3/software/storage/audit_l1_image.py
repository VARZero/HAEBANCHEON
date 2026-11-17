"""Audit the built SD payload and unchanged v5 application; no physical devices."""
import hashlib, json, struct, subprocess, sys, zlib
from pathlib import Path

out=Path(sys.argv[1]); base=Path(sys.argv[2])
image=out/'image/HAEBANCHEON-KFB-L1-32K-DMA3-v1.img'
raw=image.read_bytes()
manifest=json.loads((image.parent/'manifest.json').read_text())
assert manifest['image']==str(image)
assert hashlib.sha256(raw).hexdigest()==manifest['sha256']
assert len(raw)==manifest['bytes'] and raw[510:512]==b'\x55\xaa'
p1=struct.unpack_from('<B3sB3sII',raw,446)
p2=struct.unpack_from('<B3sB3sII',raw,462)
assert p1[2]==0xda and p2[2]==0x83 and p1[4]==2048
assert p1[4]+p1[5]<=p2[4] and (p2[4]+p2[5])*512==len(raw)
assert raw[p2[4]*512:]==(out/'image/data.ext4').read_bytes()
assert raw[p1[4]*512:p1[4]*512+8]==b'ORYSD001'
pos=p1[4]+1
for i,path in enumerate([out/'firmware/fw_jump.bin',out/'firmware/Image',out/'image/boot/gen3.dtb']):
    count,crc=struct.unpack_from('<II',raw,p1[4]*512+8+8*i)
    payload=raw[pos*512:(pos+count)*512]; expected=path.read_bytes()
    assert len(payload)==count*512 and zlib.crc32(payload)==crc
    assert payload[:len(expected)]==expected and not any(payload[len(expected):])
    pos+=count
subprocess.run(['e2fsck','-fn',str(out/'image/data.ext4')],check=True)
def inventory(root):
    return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file() and not p.is_symlink()}
old=inventory(base/'root'); new=inventory(out/'root')
changed=sorted(k for k in old.keys()|new.keys() if old.get(k)!=new.get(k))
assert changed==['etc/oryukdo-image'],changed
assert inventory(base/'data-root')==inventory(out/'data-root')
assert (base/'firmware/fw_jump.bin').read_bytes()==(out/'firmware/fw_jump.bin').read_bytes()
dt=subprocess.check_output(['dtc','-I','dtb','-O','dts',str(out/'image/boot/gen3.dtb')],text=True)
for token in ['i-cache-size = <0x8000>','d-cache-size = <0x8000>',
              'i-cache-sets = <0x200>','d-cache-sets = <0x200>',
              'riscv,ndev = <0x04>','dma-controller@10009000','dma-controller@1000a000','dma-controller@1000b000']:
    assert token in dt,token
assert dt.count('dma-coherent;')==5
result={'result':'PASS_IMAGE_AUDIT','image_sha256':manifest['sha256'],
        'root_file_changes_from_tiles_v5':changed,'data_files_identical_to_tiles_v5':True,
        'opensbi_identical_to_tiles_v5':True,'boot_payload_crc_and_exact_bytes':True,
        'ext4_check':True,'cache_dt_and_three_dma_nodes':True,'physical_board_test':False}
(out/'reports/image_audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
