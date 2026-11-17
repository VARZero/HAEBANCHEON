"""Audit the initramfs embedded in each SD kernel, source hashes, ISA and dependencies."""
from pathlib import Path
import gzip, hashlib, json, os, re, struct, subprocess, sys, zlib

here = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
sha = lambda data: hashlib.sha256(data).hexdigest()
reference = json.loads((here / 'reference.sha256.json').read_text())
for name, digest in reference.items():
    assert sha((here / 'reference' / name).read_bytes()) == digest, name
build = json.loads((out / 'boayo-native-rv32.build.json').read_text())
assert sha((out / 'boayo-native-rv32').read_bytes()) == build['binary_sha256']
for name, digest in build['source_sha256'].items():
    assert sha((here / name).read_bytes()) == digest, name
assert sha((here.parents[1] / 'bosio/include/bosio_uapi.h').read_bytes()) == build['uapi_sha256']
root = out / 'root'
audit = json.loads((out / 'root_audit.json').read_text())
for name, digest in audit['preserved'].items():
    assert sha((root / name).read_bytes()) == digest, name
elf = []
for p in sorted(root.rglob('*')):
    if not p.is_file() or p.is_symlink():
        continue
    data = p.read_bytes()
    if not data.startswith(b'\x7fELF'):
        continue
    assert data[4:6] == b'\x01\x01' and struct.unpack_from('<H', data, 18)[0] == 243, p
    flags = struct.unpack_from('<I', data, 36)[0]
    assert flags & 7 == 0, (p, flags)
    attrs = subprocess.check_output(['riscv64-linux-gnu-readelf', '-A', str(p)], text=True)
    match = re.search(r'Tag_RISCV_arch: "([^"]+)"', attrs)
    arch = match[1] if match else None
    if arch:
        parts = arch.split('_')
        assert parts[0].startswith('rv32i'), (p, arch)
        for item in parts[1:]:
            assert re.sub(r'\d.*$', '', item) in {'m','a','zicsr','zifencei','zmmul','zaamo','zalrsc'}, (p, arch)
    dynamic = subprocess.check_output(['riscv64-linux-gnu-readelf', '-d', str(p)], text=True)
    needed = re.findall(r'\(NEEDED\).*\[([^]]+)\]', dynamic)
    for name in needed:
        assert any((root / directory / name).exists() for directory in ('lib','usr/lib')), (p,name)
    elf.append(dict(file=str(p.relative_to(root)), sha256=sha(data), arch=arch, needed=needed))
assert any(e['file']=='usr/bin/boayo-native' for e in elf)
images = []
for compression in ('gzip','raw'):
    p = out / 'images' / f'ORYUKDO-KFB-BoAYO-native-v2-{compression}.img'
    disk = p.read_bytes()
    assert disk[510:512] == b'\x55\xaa' and disk[2048*512:2048*512+8] == b'ORYSD001'
    sector = 2049
    payloads = []
    for i in range(3):
        count, crc = struct.unpack_from('<II',disk,2048*512+8+i*8)
        payload = disk[sector*512:(sector+count)*512]
        assert len(payload) == count*512 and zlib.crc32(payload) == crc
        payloads.append(payload)
        sector += count
    assert sector*512 == len(disk)
    manifest = json.loads((out/compression/'manifest.json').read_text())
    assert manifest['image_sha256'] == sha(disk)
    symbols = {line.split()[2]:int(line.split()[0],16)
               for line in (out/compression/'System.map').read_text().splitlines()}
    kernel = payloads[1]
    base = symbols['_start']
    offset = symbols['__initramfs_start'] - base
    size = struct.unpack_from('<I',kernel,symbols['__initramfs_size']-base)[0]
    assert size > 0 and offset+size <= len(kernel)
    packed = kernel[offset:offset+size]
    cpio = gzip.decompress(packed) if compression=='gzip' else packed
    cursor = 0
    records = {}
    while cursor < len(cpio):
        header = cpio[cursor:cursor+110]
        assert header[:6] == b'070701'
        fields = [int(header[6+i*8:14+i*8],16) for i in range(13)]
        n, length = fields[11], fields[6]
        name = cpio[cursor+110:cursor+110+n-1].decode().lstrip('/')
        cursor = (cursor+110+n+3)&~3
        content = cpio[cursor:cursor+length]
        cursor = (cursor+length+3)&~3
        if name=='TRAILER!!!':
            break
        assert name not in records, name
        records[name] = (fields[1],content)
    staged = set()
    for file in root.rglob('*'):
        name = str(file.relative_to(root))
        staged.add(name)
        mode, content = records[name]
        assert (mode&0o7777) == (file.lstat().st_mode&0o7777), name
        if file.is_symlink():
            # Linux gen_init_cpio includes the link target's terminating NUL.
            assert mode&0o170000==0o120000 and content==os.readlink(file).encode()+b'\0', name
        elif file.is_file():
            assert mode&0o170000==0o100000 and content==file.read_bytes(), name
        else:
            assert mode&0o170000==0o040000, name
    assert set(records)-staged == {'dev/console'}
    assert not any('python' in n.lower() or 'numpy' in n.lower() or '/PIL/' in n for n in records)
    images.append(dict(file=p.name,bytes=len(disk),sha256=sha(disk),initramfs_bytes=size,
                       cpio_bytes=len(cpio),checked_entries=len(records),kernel_payload_sha256=sha(kernel)))
report = dict(result='PASS',images=images,reference_files=len(reference),elf=elf,
              source_manifest=build,root=audit,hardware_tested=False,rtl_tested=False)
(out/'reports/release_audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(result='PASS',images=images,reference_files=len(reference),elf_files=len(elf))))
