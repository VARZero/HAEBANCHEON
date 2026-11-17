"""Archive completed local evidence and copy the audited image; no board access."""
from pathlib import Path
import difflib, hashlib, json, shutil, sys

variant = Path(__file__).resolve().parents[1]
work, destination = map(Path, sys.argv[1:3])
evidence = variant / 'evidence'
evidence.mkdir(exist_ok=True)
audit = json.loads((work / 'audit.json').read_text())
qemu = json.loads((work / 'qemu/result.json').read_text())
assert audit['result'] == 'PASS_ARTIFACT_AUDIT'
assert qemu['result'] == 'PASS_QEMU_ONLY'
assert audit['image_sha256'] == qemu['image_sha256']
manifests = {}
for mode in ('host', 'rv32', 'sanitize', 'reference'):
    suffix = 'host' if mode == 'reference' else mode
    p = work / mode / ('boayo-native-' + suffix + '.build.json')
    manifests[mode] = json.loads(p.read_text())
    shutil.copy2(p, evidence / (mode + '-build.json'))
assert manifests['host']['source_sha256'] == manifests['rv32']['source_sha256'] == manifests['sanitize']['source_sha256']
for folder in ('reports-final', 'extra', 'sanitizer-reports', 'qemu'):
    for p in (work / folder).rglob('*'):
        if p.is_file() and p.suffix in ('.json', '.log'):
            target = evidence / p.relative_to(work)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, target)
for name in ('audit.json', 'rect-assets.json'):
    shutil.copy2(work / name, evidence / name)
for folder in ('assets', 'plans'):
    shutil.copy2(work / folder / 'manifest.json', evidence / (folder + '-manifest.json'))
for name in ('config.log', 'kernel.log', 'image.log'):
    shutil.copy2(work / 'release-final' / name, evidence / name)
# Keep the failed, fixed intermediate trial clearly separate from final PASS logs.
old = work / 'attempt1/reuse-background-inflation.log'
if old.exists():
    (evidence / 'resolved-attempt1').mkdir(exist_ok=True)
    shutil.copy2(old, evidence / 'resolved-attempt1' / old.name)
# Reproducible rectangular-UI comparison reference, based on the preserved v13.
reference = Path(manifests['reference']['source'])
base = variant.parent / 'boayo-geometry-v13/software/boayo/native'
patch = []
for name in manifests['reference']['source_sha256']:
    if not name.startswith('src/'):
        continue
    old, new = (base / name).read_text(), (reference / name).read_text()
    if old != new:
        patch.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True), 'a/' + name, 'b/' + name))
(variant / 'tests/reference-ui.patch').write_text(''.join(patch))
source_check = dict(result='PASS_IDENTICAL_HOST_RV32_SANITIZER_SOURCES', files=len(manifests['host']['source_sha256']))
(evidence / 'source-consistency.json').write_text(json.dumps(source_check, indent=2) + '\n')
source_image = work / 'release-final/image/HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img'
destination.mkdir(parents=True, exist_ok=True)
target_image = destination / source_image.name
assert hashlib.file_digest(source_image.open('rb'), 'sha256').hexdigest() == audit['image_sha256']
shutil.copy2(source_image, target_image)
assert hashlib.file_digest(target_image.open('rb'), 'sha256').hexdigest() == audit['image_sha256']
(destination / (target_image.name + '.sha256')).write_text(audit['image_sha256'] + '  ' + target_image.name + '\n')
release = dict(image=target_image.name, image_bytes=target_image.stat().st_size, image_sha256=audit['image_sha256'],
    app_sha256=audit['app_sha256'], kernel_sha256=audit['kernel_sha256'],
    required_bitstream='HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit',
    bitstream_sha256='9bb855886be0ac55d983f9a72ab0e6c23317b7f4fe27e13430cb2de17d7d4e07',
    rtl_changed=False, driver_changed=False, hardware_tested=False, physical_timing_measured=False,
    local_result='PASS', qemu_result=qemu['result'], mock_backend=True)
for folder in (evidence, destination):
    (folder / 'release-manifest.json').write_text(json.dumps(release, indent=2) + '\n')
print(json.dumps(release, indent=2))
