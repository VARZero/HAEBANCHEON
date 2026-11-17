"""Package precomputed projection plans; require exact-source validation."""
from pathlib import Path
import difflib,hashlib,json,re,shutil,sys,zipfile
here=Path(__file__).resolve().parent
work=Path(sys.argv[1]);dest=Path(sys.argv[2]);evidence=here/'evidence'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(a,b):b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b)
audit=json.loads((work/'reports/artifact.json').read_text())
suite=json.loads((work/'reports/host/suite.json').read_text())
trace=json.loads((work/'reports/assets/result.json').read_text())
san=json.loads((work/'reports/assets-sanitize/result.json').read_text())
assert san['result']=='PASS_ASSETS_AND_PROJECTED_SCENES'
qemu=json.loads((work/'reports/qemu/result.json').read_text())
plans=json.loads((work/'reports/plans/result.json').read_text())
plans_san=json.loads((work/'reports/plans-sanitize/result.json').read_text())
pm=json.loads((work/'release/data-root/boayo-plans/manifest.json').read_text())
for directory,binary in [('resident','host/boayo-native-host'),('resident-sanitize','sanitize/boayo-native-sanitize')]:
    r=json.loads((work/'reports'/directory/'result.json').read_text())
    assert r['result']=='PASS_RESIDENT_TILES' and len(r['scenes'])==42
    assert r['candidate_sha256']==sha(work/binary)
    assert r['scenes'][0]['batch_requests']==[44,44]
for directory,binary in [('versions','host/boayo-native-host'),('versions-sanitize','sanitize/boayo-native-sanitize')]:
    vr=json.loads((work/'reports'/directory/'result.json').read_text())
    assert vr['result']=='PASS_LAZY_VERSIONS' and vr['candidate_sha256']==sha(work/binary)
assert qemu['first_projection']['packed_mask_tiles_total']==44
assert qemu['first_projection']['plan_cache']['builtin_loads']==1
assert qemu['first_projection']['plan_cache']['payload_scan_bytes']==0
assert qemu['first_projection']['mode']=='ddr-resident-dirty-tiles'
assert qemu['first_projection']['cpu_scene_words']==qemu['first_projection']['cpu_result_download_words']==0
assert qemu['first_projection']['cpu_output_shadow_words']==4480
assert plans['result']==plans_san['result']=='PASS_PRECOMPUTED_PLANS'
assert plans['candidate_sha256']==sha(work/'host/boayo-native-host')
assert plans_san['candidate_sha256']==sha(work/'sanitize/boayo-native-sanitize')
assert suite['binary_sha256']==sha(work/'host/boayo-native-host')
app_manifests=[json.loads(p.read_text()) for p in (
 work/'host/boayo-native-host.build.json',work/'release/apps/boayo-native-rv32.build.json',
 work/'sanitize/boayo-native-sanitize.build.json')]
assert all(m['source_sha256']==app_manifests[0]['source_sha256'] for m in app_manifests)
assert audit['result']=='PASS_ARTIFACT_AUDIT'
assert suite['result']=='PASS' and len(suite['tests'])==9
asset_suite=json.loads((work/'reports/host-assets/suite.json').read_text())
assert asset_suite['result']=='PASS' and len(asset_suite['tests'])==9
assert trace['result']=='PASS_ASSETS_AND_PROJECTED_SCENES'
assert qemu['result']=='PASS_QEMU_ONLY' and qemu['image_sha256']==audit['image_sha256']
plan=(work/'release/data-root/boayo-plans'/pm['bootstrap']).read_bytes()
assert qemu['rv32_plan_payload_sha256']==hashlib.sha256(plan[4096:]).hexdigest()
assert qemu['first_projection']['geometry_tiles_evaluated_total']==0
assert qemu['first_projection']['mask_samples_evaluated_total']==0
uart=(work/'reports/qemu/uart.log').read_text(errors='replace')
assert 'Linux version 6.12.0-oryukdo-tiles4' in uart
events=re.findall(r'BOAYO_INITTRACE n=(\d+) event=(\S+)',uart)
assert [int(n) for n,e in events]==list(range(1,len(events)+1))
assert 'asset.loaded' in [e for n,e in events]
assert not {'rounded.begin','circle.begin','rgb.dma.begin','surface.copy.begin'}.intersection(e for n,e in events)
assert events[-2][1]=='desktop.end' and events[-1][1]=='desktop.scope_exit'
assert 'PASS BS25 actual RTL' in (work/'reports/scene-rtl.log').read_text()
assert 'PASS native SD boot initialization' in (work/'reports/sd-rtl.log').read_text()
assert 'PASS boot-to-runtime ownership' in (work/'reports/sd-rtl.log').read_text()
evidence.mkdir(exist_ok=True)
for name in ('host','host-assets','assets','assets-sanitize','plans','plans-sanitize','resident','resident-sanitize','versions','versions-sanitize','qemu'):shutil.copytree(work/'reports'/name,evidence/name,dirs_exist_ok=True)
for name in ('publish-profile-qemu.json','publish-profile-qemu.log','build-plan-gate.log','artifact.json','artifact.log','scene-rtl.log','scene-rtl-build.log','scene-rtl-sources.txt','sd-rtl.log','sd-rtl-build.log','sd-rtl-sources.txt','assets-test.log','assets-sanitize.log','sanitize-build.log','plans-test.log','plans-sanitize.log','resident-test.log','resident-sanitize.log','qemu-test.log'):copy(work/'reports'/name,evidence/name)
if (work/'reports/build.log').exists():copy(work/'reports/build.log',evidence/'build.log')
copy(work/'release/data-root/boayo-assets/manifest.json',evidence/'asset_manifest.json')
copy(work/'release/data-root/boayo-plans/manifest.json',evidence/'plan_manifest.json')
copy(work/'release/data-root/boayo-plans'/pm['bootstrap'],evidence/pm['bootstrap'])
for name in ('kernel.log','config.log','image.log'):copy(work/'release'/name,evidence/name)
copy(work/'release/kernel/.config',evidence/'linux.config')
copy(work/'release/apps/boayo-native-rv32.build.json',evidence/'app-rv32.build.json')
copy(work/'host/boayo-native-host.build.json',evidence/'app-host.build.json')
copy(work/'sanitize/boayo-native-sanitize.build.json',evidence/'app-sanitize.build.json')
copy(work/'release/reports/driver.log',evidence/'driver.log')
for name in ('host_build.log','app_build.log','preflight-resident.log','preflight-versions.log'):copy(work/'release/reports'/name,evidence/name)
if (work/'reports/resolved-worker-stack-failure.log').exists():copy(work/'reports/resolved-worker-stack-failure.log',evidence/'resolved-worker-stack-failure.log')
assert 'PASS DIRECT actual-driver source-page DMA' in (evidence/'driver.log').read_text()
assert 'PASS RGB DMA actual driver' in (evidence/'driver.log').read_text()
assert 'PASS PRESENT actual engine' in (evidence/'driver.log').read_text()
assert 'PASS BUILD_PLAN_GATE' in (evidence/'build-plan-gate.log').read_text()
assert 'PASS PUBLISH actual engine' in (evidence/'driver.log').read_text()
assert 'PASS BATCH actual-kernel helper' in (evidence/'driver.log').read_text()
assert 'PASS TILE actual-kernel helper 1024' in (evidence/'driver.log').read_text()
# Include focused changes; original DMA overlay remains a separate baseline.
diff=[]
for after in sorted((here/'software').rglob('*')):
    if not after.is_file() or '__pycache__' in after.parts:continue
    name=after.relative_to(here).as_posix()
    before=here.parent/'boayo-tiles-v3'/name
    if before.exists() and before.read_bytes()==after.read_bytes():continue
    diff.extend(difflib.unified_diff(before.read_text().splitlines(True) if before.exists() else [],
        after.read_text().splitlines(True),fromfile='boayo-tiles-v3/'+name,tofile='boayo-tiles-v4/'+name))
(evidence/'changes.patch').write_text(''.join(diff),encoding='utf-8')
image=work/'release/image/ORYUKDO-KFB-BoAYO-tiles-v4.img'
assert sha(image)==audit['image_sha256']
dest.mkdir(parents=True,exist_ok=True)
target=dest/image.name
if target.exists():assert sha(target)==sha(image),'refuse replacement of different diagnostic image'
else:copy(image,target)
assert sha(target)==audit['image_sha256']
report=dict(result='PASS_LOCAL_RESIDENT_TILE_IMAGE_HARDWARE_PENDING',image=str(target),
 image_sha256=sha(target),bytes=target.stat().st_size,kernel='6.12.0-oryukdo-tiles4',
 host_regression_tests=9,host_configurations=2,pixel_and_scene_equivalence='PASS',trace_events=len(events),precomputed_variants=132,
 qemu_uart_trace='PASS',qemu_boayo='PASS_MOCK_BACKEND',sd_rtl='PASS_MOCK_DDR',bs25_scene_rtl='PASS_EXISTING_RTL',lazy_versions='PASS_HOST_AND_SANITIZER',
 bitstream_changed=False,hardware_tested=False,performance_measured=False,
 rgb_dma_driver_boundary_model='PASS_480_CASES_AND_FAILURE_PATHS',
 direct_dma_driver_boundary_model='PASS_36_CASES_AND_FAILURE_PATHS',
 mapped_texture_asan_ubsan='PASS',
 scope='Fused metadata upload/present from CPU snapshot; per-stage profiling; prepacked boundary masks; preserved tile scheduling',
 fused_metadata_publication=True,prepacked_boundary_masks=True,publish_stage_profiling=True,
 previous_board_publish_seconds=87.729877,physical_substage_timings='PENDING_NEW_SD_BOARD_RUN',publish_driver_boundary='PASS',
 integer_tile_identities=True,builtin_payload_runtime_scan_bytes=0,initial_scalar_requests_before=88,initial_batch_requests_after=2,batch_max_entries=128,batch_driver_boundary='PASS',
 cpu_scene_bytes=0,cpu_output_metadata_bytes=17920,cpu_readback_bytes=0,
 lazy_tile_versions=True,post_present_mirror=False,conditional_metadata_reload=True,reactivation_reuse=True,metadata_write_words=4476,initial_direct_tiles=52,initial_masked_tiles=44,initial_tile_fill_bytes=45056,
 resident_scene_equivalence_cases=42,tile_helper_cases=1024,
 driver_changed=True,reserved_arena_zeroing=False,default_renderer='ddr-resident-dirty-tiles',
 plan_bytes=len(plan),plan_tiles=160,projector_jobs=96,empty_jobs_omitted=64,
 first_launcher_geometry_candidates=0,first_launcher_mask_samples=0,
 rv32_plan_equivalence='PASS_EXACT_DESCRIPTOR_AND_MASK_BYTES',plan_asan_ubsan='PASS',
 remaining_cpu_work='file/page/DMA management, dynamic 2D drawing, uncached geometry/masks, metadata, partial-word stencil edges; explicit legacy renderer retained',
 bitstream_sha256='fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0')
(evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
sources={p.relative_to(here).as_posix():sha(p) for p in sorted(here.rglob('*'))
 if p.is_file() and 'evidence' not in p.relative_to(here).parts and '__pycache__' not in p.parts}
(evidence/'source_sha256.json').write_text(json.dumps(sources,indent=2)+'\n')
shutil.copytree(evidence,dest/'evidence',dirs_exist_ok=True)
copy(here/'README_KO.md',dest/'README_KO.md');copy(evidence/'result.json',dest/'result.json')
copy(here/'검증_변경기록.md',dest/'검증_변경기록.md')
archive=dest/'BoAYO-tiles-v4-source-and-tests.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(here.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts:z.write(p,'boayo-tiles-v4/'+p.relative_to(here).as_posix())
(dest/'SHA256SUMS.txt').write_text('\n'.join(sha(p)+'  '+p.name for p in (target,archive))+'\n')
print(json.dumps(report,indent=2))
