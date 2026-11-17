from pathlib import Path
import hashlib,json,sys
here=Path(__file__).resolve().parents[1]
manifest={str(p.relative_to(here/'reference')):hashlib.sha256(p.read_bytes()).hexdigest()
          for p in sorted((here/'reference').rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
target=here/'reference.sha256.json'
if target.exists():assert json.loads(target.read_text())==manifest,'Python reference was modified'
else:target.write_text(json.dumps(manifest,indent=2)+'\n')
sys.path[:0]=[str(here/'reference/wm'),str(here/'reference/apps')]
from bosio_window_gui import FONT_5X7
rows=[]
for code in range(128):
 glyph=FONT_5X7.get(chr(code),FONT_5X7[' '])
 rows.append('{'+','.join(str(sum((1<<(4-x)) for x,c in enumerate(row) if c=='#')) for row in glyph)+'}')
(here/'src/font.hpp').write_text('#pragma once\n#include <cstdint>\nnamespace boayo { inline constexpr uint8_t font[128][7]={\n'+',\n'.join(rows)+'\n}; }\n')
print('PASS frozen Python reference',len(manifest),'files')
