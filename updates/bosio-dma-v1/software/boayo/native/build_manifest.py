from pathlib import Path
import sys,json,hashlib
source,binary=map(Path,sys.argv[1:])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[p for d in ('src','vendor','reference') for p in (source/d).rglob('*') if p.is_file()]
manifest=dict(binary_sha256=sha(binary),source=str(source),source_sha256={str(p.relative_to(source)):sha(p) for p in sorted(files)},uapi_sha256=sha(source.parents[1]/'bosio/include/bosio_uapi.h'))
binary.with_suffix('.build.json').write_text(json.dumps(manifest,indent=2)+'\n')
