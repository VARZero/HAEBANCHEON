from pathlib import Path
import shutil,subprocess,tempfile
n=Path(__file__).resolve().parent.parent/'software/boayo/native'
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 shutil.copytree(n/'src',p/'src')
 for f in ('plan_revision.py','plan_compatibility.json'):shutil.copy2(n/f,p/f)
 subprocess.run(['python3',str(p/'plan_revision.py')],check=True)
 assert 'BOAYO_PLAN_COMPAT_REV' in (p/'src/plan_revision.hpp').read_text()
 with (p/'src/mask.hpp').open('a') as f:f.write('\n// Unverified future change must revoke payload compatibility.\n')
 subprocess.run(['python3',str(p/'plan_revision.py')],check=True)
 assert 'BOAYO_PLAN_COMPAT_REV' not in (p/'src/plan_revision.hpp').read_text()
print('PASS_COMPATIBILITY_HASH_GUARD')
