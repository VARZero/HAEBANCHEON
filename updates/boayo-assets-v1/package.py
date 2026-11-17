"""Package prebuilt UI and texture reuse, keeping board validation pending."""
from pathlib import Path
import difflib,hashlib,json,re,shutil,sys,zipfile
here=Path(__file__).resolve().parent
work=Path(sys.argv[1]);dest=Path(sys.argv[2]);evidence=here/'evidence'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(a,b):b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b)
audit=json.loads((work/'reports/artifact.json').read_text())
suite=json.loads((work/'reports/host/suite.json').read_text())
trace=json.loads((work/'reports/assets/result.json').read_text())
qemu=json.loads((work/'reports/qemu/result.json').read_text())
assert audit['result']=='PASS_ARTIFACT_AUDIT'
assert suite['result']=='PASS' and len(suite['tests'])==9
asset_suite=json.loads((work/'reports/host-assets/suite.json').read_text())
assert asset_suite['result']=='PASS' and len(asset_suite['tests'])==9
assert trace['result']=='PASS_ASSETS_AND_PROJECTED_SCENES'
assert qemu['result']=='PASS_QEMU_ONLY' and qemu['image_sha256']==audit['image_sha256']
uart=(work/'reports/qemu/uart.log').read_text(errors='replace')
assert 'Linux version 6.12.0-oryukdo-assets1' in uart
events=re.findall(r'BOAYO_INITTRACE n=(\d+) event=(\S+)',uart)
assert [int(n) for n,e in events]==list(range(1,len(events)+1))
assert 'asset.loaded' in [e for n,e in events]
assert 'rounded.begin' not in [e for n,e in events]
assert events[-2][1]=='desktop.end' and events[-1][1]=='desktop.scope_exit'
assert 'PASS native SD boot initialization' in (work/'reports/sd-rtl.log').read_text()
assert 'PASS boot-to-runtime ownership' in (work/'reports/sd-rtl.log').read_text()
evidence.mkdir(exist_ok=True)
for name in ('host','host-assets','assets','qemu'):shutil.copytree(work/'reports'/name,evidence/name,dirs_exist_ok=True)
for name in ('artifact.json','artifact.log','sd-rtl.log','assets-test.log'):copy(work/'reports'/name,evidence/name)
copy(work/'release/data-root/boayo-assets/manifest.json',evidence/'asset_manifest.json')
for name in ('kernel.log','config.log','image.log'):copy(work/'release'/name,evidence/name)
copy(work/'release/kernel/.config',evidence/'linux.config')
copy(work/'release/apps/boayo-native-rv32.build.json',evidence/'app-rv32.build.json')
copy(work/'host/boayo-native-host.build.json',evidence/'app-host.build.json')
copy(work/'release/reports/driver.log',evidence/'driver.log')
assert 'PASS RGB DMA actual driver' in (evidence/'driver.log').read_text()
# Include focused changes; original DMA overlay remains a separate baseline.
diff=[]
for after in sorted((here/'software').rglob('*')):
    if not after.is_file() or '__pycache__' in after.parts:continue
    name=after.relative_to(here).as_posix()
    before=here.parent/'boayo-rgbdma-v1'/name
    if before.exists() and before.read_bytes()==after.read_bytes():continue
    diff.extend(difflib.unified_diff(before.read_text().splitlines(True) if before.exists() else [],
        after.read_text().splitlines(True),fromfile='boayo-rgbdma-v1/'+name,tofile='boayo-assets-v1/'+name))
(evidence/'changes.patch').write_text(''.join(diff),encoding='utf-8')
image=work/'release/image/ORYUKDO-KFB-BoAYO-assets-v1.img'
assert sha(image)==audit['image_sha256']
dest.mkdir(parents=True,exist_ok=True)
target=dest/image.name
if target.exists():assert sha(target)==sha(image),'refuse replacement of different diagnostic image'
else:copy(image,target)
assert sha(target)==audit['image_sha256']
report=dict(result='PASS_LOCAL_PREBUILT_UI_IMAGE_HARDWARE_PENDING',image=str(target),
 image_sha256=sha(target),bytes=target.stat().st_size,kernel='6.12.0-oryukdo-assets1',
 host_regression_tests=9,host_configurations=2,pixel_and_scene_equivalence='PASS',trace_events=len(events),precomputed_variants=132,
 qemu_uart_trace='PASS',qemu_boayo='PASS_MOCK_BACKEND',sd_rtl='PASS_MOCK_DDR',
 bitstream_changed=False,hardware_tested=False,performance_measured=False,
 rgb_dma_driver_boundary_model='PASS_480_CASES_AND_FAILURE_PATHS',
 scope='132 prebuilt UI variants, RGB and projector-word expansion cache, initial publish move, immutable texture resident reuse',
 remaining_cpu_work='asset expansion/copies, dynamic drawing, masks and final tile composition',
 bitstream_sha256='fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0')
(evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
sources={p.relative_to(here).as_posix():sha(p) for p in sorted(here.rglob('*'))
 if p.is_file() and 'evidence' not in p.relative_to(here).parts and '__pycache__' not in p.parts}
(evidence/'source_sha256.json').write_text(json.dumps(sources,indent=2)+'\n')
shutil.copytree(evidence,dest/'evidence',dirs_exist_ok=True)
copy(here/'README_KO.md',dest/'README_KO.md');copy(evidence/'result.json',dest/'result.json')
archive=dest/'BoAYO-assets-v1-source-and-tests.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(here.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts:z.write(p,'boayo-assets-v1/'+p.relative_to(here).as_posix())
(dest/'SHA256SUMS.txt').write_text('\n'.join(sha(p)+'  '+p.name for p in (target,archive))+'\n')
print(json.dumps(report,indent=2))
