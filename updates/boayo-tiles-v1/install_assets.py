from pathlib import Path
import json,shutil,sys
assets=Path(sys.argv[1]);root=Path(sys.argv[2]);manifest=json.loads((assets/'manifest.json').read_text())
dest=root/'usr/share/boayo/assets';dest.mkdir(parents=True,exist_ok=True)
# The first launcher must work even without /data (also exercises the exact
# release kernel asset path under QEMU). Other 131 variants live on SD ext4.
shutil.copy2(assets/manifest['bootstrap'],dest/manifest['bootstrap'])
(dest/'bootstrap.json').write_text(json.dumps(dict(file=manifest['bootstrap'],format=manifest['format']))+'\n')
