"""Collect local evidence and copy the audited candidate without publishing."""
from pathlib import Path
import difflib,hashlib,json,shutil,sys,zipfile
here=Path(__file__).resolve().parent
repo=here.parent.parent
work=Path(sys.argv[1]);dest=Path(sys.argv[2]);bit=Path(sys.argv[3])
evidence=here/'evidence';evidence.mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cp(src,dst):
    dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
for folder in ('host-final','sanitize-final','qemu-final','qemu'):
    shutil.copytree(work/'reports'/folder,evidence/folder,dirs_exist_ok=True)
for source,name in [('baseline/reports','baseline'),('apps/reports','first-counter-failure')]:
    shutil.copytree(work/source,evidence/name,dirs_exist_ok=True)
for name in ('driver-final.log','sd-rtl.log','artifact.json','artifact.log'):
    cp(work/'reports'/name,evidence/name)
cp(work/'output.log',evidence/'output.log')
cp(here/'software/bosio/reports/engine.log',evidence/'engine.log')
for name in ('kernel.log','config.log','image.log'):
    cp(work/'release'/name,evidence/name)
for p in (work/'release/reports').iterdir():
    if p.is_file():cp(p,evidence/'build'/p.name)
cp(work/'release/kernel/.config',evidence/'linux.config')
for mode,folder in [('host','apps-final'),('sanitize','sanitize-final'),('rv32','release/apps')]:
    cp(work/folder/f'boayo-native-{mode}.build.json',evidence/f'app-{mode}.build.json')
audit=json.loads((evidence/'artifact.json').read_text())
for name in ('baseline','host-final','sanitize-final'):
    assert json.loads((evidence/name/'suite.json').read_text())['result']=='PASS',name
qemu=json.loads((evidence/'qemu-final/result.json').read_text())
assert qemu['result']=='PASS_QEMU_ONLY' and qemu['image_sha256']==audit['image_sha256']
assert qemu['final_output']['dma_copied_words']>0
assert qemu['final_output']['memory_dma']['available']
assert audit['result']=='PASS_ARTIFACT_AUDIT'
assert 'PASS actual DMA wait/upload' in (evidence/'driver-final.log').read_text()
assert 'PASS CPU-vs-DMA' in (evidence/'output.log').read_text()
assert 'PASS native SD boot initialization' in (evidence/'sd-rtl.log').read_text()
assert 'PASS boot-to-runtime ownership' in (evidence/'sd-rtl.log').read_text()
expected_bit='fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0'
assert sha(bit)==expected_bit
image=work/'release/image/ORYUKDO-KFB-BOSIO-DMA-v1.img'
assert sha(image)==audit['image_sha256']
dest.mkdir(parents=True,exist_ok=True)
target=dest/image.name
if target.exists():assert sha(target)==audit['image_sha256'],'refuse replacing different published candidate'
else:cp(image,target)
assert sha(target)==audit['image_sha256']
report=dict(result='PASS_LOCAL_HARDWARE_PENDING',image=str(target),
 image_sha256=audit['image_sha256'],image_bytes=target.stat().st_size,
 bitstream=str(bit),bitstream_sha256=expected_bit,rtl_changed=False,
 checks={'host_baseline':9,'host_final':9,'asan_ubsan_final':9,'engine':28,
 'driver_boundary_model':'PASS','both_bank_equality':'PASS','artifact_identity':'PASS',
 'sd_rtl_loader':'PASS_MOCK_DDR','qemu':'PASS_MOCK_BOSIO'},
 traffic_example={'cpu_uploaded_words':34872,'dma_uploaded_words':17436,'ddr_copied_words':17436},
 physical_dma_tested=False,physical_hdmi_tested=False,physical_performance_measured=False,
 synthesis_rerun=False,remaining=['실보드 DMA/HDMI/WASD 및 시간 측정','전원 차단 후 /data 정합성·영속성 확인'])
(evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
# Preserve the relevant pre-change text as a reviewable patch, not an altered baseline.
diff=[]
baseline=json.loads((work/'baseline/boayo-native-host.build.json').read_text())
for rel in ('src/backend.cpp','src/backend.hpp','src/selftest.cpp'):
    before=Path(baseline['source'])/rel
    # WSL paths need conversion when collecting on Windows.
    if not before.exists() and work.as_posix().startswith('//wsl.localhost/Ubuntu/'):
        before=Path('//wsl.localhost/Ubuntu')/before.as_posix().lstrip('/')
    after=here/'software/boayo/native'/rel
    diff.extend(difflib.unified_diff(before.read_text().splitlines(True),after.read_text().splitlines(True),
                                   fromfile='baseline/'+rel,tofile='bosio-dma-v1/'+rel))
(evidence/'boayo_changes.patch').write_text(''.join(diff),encoding='utf-8')
source_hashes={p.relative_to(here).as_posix():sha(p) for p in sorted(here.rglob('*'))
 if p.is_file() and 'evidence' not in p.relative_to(here).parts and '__pycache__' not in p.parts}
(evidence/'source_sha256.json').write_text(json.dumps(source_hashes,indent=2)+'\n')
shutil.copytree(evidence,dest/'evidence',dirs_exist_ok=True)
cp(here/'README_KO.md',dest/'README_KO.md')
cp(evidence/'result.json',dest/'result.json')
archive=dest/'BOSIO-DMA-v1-source-and-tests.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(here.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts:z.write(p,'bosio-dma-v1/'+p.relative_to(here).as_posix())
(dest/'SHA256SUMS.txt').write_text('\n'.join(sha(p)+'  '+p.name for p in (target,archive))+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
