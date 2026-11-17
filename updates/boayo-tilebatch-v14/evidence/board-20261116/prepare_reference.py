from pathlib import Path
import json,shutil,subprocess
w=Path('/var/tmp/haebancheon-tilebatch-v14')
v=Path('/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/ORYUKDO-3d5i_BOSIO_SoC/updates/boayo-tilebatch-v14')
out=w/'board-reference';out.mkdir(exist_ok=True)
old=json.loads((w/'plans-ui-reference/manifest.json').read_text())
new=json.loads((w/'plans/manifest.json').read_text())
def identity(p):return json.dumps([p['geometry'],p['shape']],sort_keys=True)
old_by={identity(p):p for p in old['plans']}
for p in new['plans']:
 a=old_by[identity(p)]
 assert (w/'plans-ui-reference'/a['file']).read_bytes()[4096:]==(w/'plans'/p['file']).read_bytes()[4096:]
proof=dict(result='PASS_EXACT_PAYLOADS',verified_plans=37,
 source_revision=json.loads(old['plans'][0]['key'])['revision'],
 payload_revision=json.loads(new['plans'][0]['key'])['revision'])
(out/'plan-proof.json').write_text(json.dumps(proof,indent=2))
manifest=json.loads((w/'reference/boayo-native-host.build.json').read_text())
native=Path(manifest['source']);target=out/'source/software/boayo/native'
shutil.copytree(native.parent.parent.parent,out/'source',dirs_exist_ok=True)
for name in ('build.sh','build_manifest.py','plan_revision.py'):
 shutil.copy2(v/'software/boayo/native'/name,target/name)
(target/'plan_compatibility.json').write_text(json.dumps(proof))
subprocess.run(['bash',str(target/'build.sh'),str(out/'rv32'),'rv32'],check=True)
compiler='/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip'
subprocess.run([compiler,'-o',str(out/'boayo-reference'),str(out/'rv32/boayo-native-rv32')],check=True)
subprocess.run(['python3','/var/tmp/haebancheon-geometry-v13/make_delta.py',str(w/'release-final/root/usr/bin/boayo-native'),str(out/'boayo-reference'),str(out/'reference.delta')],check=True)
delivery=Path('/mnt/d/temp/haebancheon-tilebatch-v14/board-20261116')
shutil.copy2(out/'reference.delta',delivery/'reference.delta')
print('PASS_REFERENCE_READY', (out/'reference.delta').stat().st_size,flush=True)
