from pathlib import Path
import json,shutil,sys
plans=Path(sys.argv[1]);root=Path(sys.argv[2]);m=json.loads((plans/'manifest.json').read_text())
dest=root/'usr/share/boayo/plans';dest.mkdir(parents=True,exist_ok=True)
for old in dest.glob('p_*.bgp'):old.unlink()
shutil.copy2(plans/m['bootstrap'],dest/m['bootstrap'])
