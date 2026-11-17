"""Verify the separately fetched official HDMI IP; unused example IP is not required."""
from pathlib import Path
import hashlib, json

def verify():
    selected=Path(__file__).resolve().parent/'vendor/digilent_rgb2dvi'
    manifest=json.loads((selected/'upstream_sources.json').read_text())
    for item in manifest['files']:
        path=selected/item['file']
        if not path.is_file():
            raise RuntimeError('HDMI IP missing; run python scripts/prepare_current_hdmi.py --all from repository root: '+str(path))
        if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
            raise RuntimeError('HDMI upstream hash mismatch: '+str(path))
    return len(manifest['files']),manifest['commit']

if __name__=='__main__':
    count,commit=verify()
    print(f'PASS EXTERNAL_HDMI_INTEGRITY files={count} official_commit={commit}')
