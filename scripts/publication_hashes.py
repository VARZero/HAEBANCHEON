"""원본 구현 증거를 보존하면서 명시된 게시용 도구 변경만 구분한다."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check_published(base, name, expected):
    path = base / name
    # Unused board-example HDMI is no longer an input of any supported build.
    # Keep its historical hashes, but never silently count it as verified RTL.
    if name.startswith('soc/board/vendor/hdmi/') and path.suffix == '.vhd':
        return 'external_unused_example'
    raw = path.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual == expected:
        return 'exact'
    if name.endswith('.sh') and hashlib.sha256(raw.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')).hexdigest() == expected:
        return 'shell_eol_only'
    rel = path.relative_to(ROOT).as_posix()
    changes = json.loads((ROOT / 'docs/HDMI_EXTERNALIZATION_HASHES.json').read_text(encoding='utf-8'))
    item = changes.get(rel)
    if item and expected in item['previous_sha256'] and actual == item['published_sha256']:
        return 'publication_tool_update'
    raise AssertionError('Unrecorded source change: ' + rel)
