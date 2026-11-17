"""현재 HW의 외부 HDMI IP를 고정된 upstream에서 준비/검사한다. IP 본문은 Git 제외."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'variants/l1-16k-dma3-lru-ram/soc/board/vendor/digilent_rgb2dvi'
CONFIGURATIONS = {
    'root': ROOT,
    **{name: ROOT / 'variants' / name for name in
       ('sd4-dma', 'l1-16k-dma3', 'l1-32k-dma3', 'l1-16k-dma3-lru-ram')},
}


def prepare(destination, check_only=False, manifest_path=None):
    manifest = json.loads((manifest_path or DEFAULT / 'upstream_sources.json').read_text(encoding='utf-8'))
    prefix = ('https://raw.githubusercontent.com/Digilent/vivado-library/'
              + manifest['commit'] + '/ip/rgb2dvi/')
    count = 0
    for item in manifest['files']:
        rel = PurePosixPath(item['file'])
        if rel.is_absolute() or '..' in rel.parts or item['url'] != prefix + str(rel):
            raise ValueError('Untrusted upstream entry: ' + str(rel))
        path = destination.joinpath(*rel.parts)
        if path.exists():
            data = path.read_bytes()
        elif check_only:
            raise FileNotFoundError(path)
        else:
            with urlopen(item['url'], timeout=60) as response:
                data = response.read()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Hash mismatch; existing files are never overwritten: ' + str(path))
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as out:
                out.write(data)
        count += 1
    print(f'PASS HDMI upstream files={count} commit={manifest["commit"]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--configuration', choices=CONFIGURATIONS, default='l1-16k-dma3-lru-ram')
    parser.add_argument('--all', action='store_true', help='prepare all five published configurations')
    parser.add_argument('--destination', type=Path,
                        help='fresh-checkout test or alternate copy; build defaults to the variant vendor path')
    args = parser.parse_args()
    if args.all and args.destination:
        parser.error('--all and --destination cannot be combined')
    for name in (CONFIGURATIONS if args.all else [args.configuration]):
        vendor = CONFIGURATIONS[name] / 'soc/board/vendor/digilent_rgb2dvi'
        print('Configuration: ' + name)
        prepare((args.destination or vendor).resolve(), args.check_only, vendor / 'upstream_sources.json')
