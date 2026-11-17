"""현재 비트스트림의 보존 소스/검증 기록을 검사한다. 합성·실보드 재검증이 아니다."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REL = Path('variants/l1-16k-dma3-lru-ram')
BASE = ROOT / REL
EXTERNAL = ('soc/board/vendor/hdmi/', 'soc/board/vendor/digilent_rgb2dvi/')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(require_tracked=False):
    manifest = json.loads((BASE / 'provenance/implementation_source_manifest.json').read_text())
    local, external = 0, 0
    shell_eol_only = []
    for name, expected in manifest.items():
        if name.startswith(EXTERNAL):
            external += 1
            continue
        if digest(BASE / name) != expected:
            # The original Windows inventory recorded CRLF shell scripts;
            # .gitattributes deliberately publishes them as executable LF text.
            raw = (BASE / name).read_bytes()
            original = raw.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
            assert name.endswith('.sh') and hashlib.sha256(original).hexdigest() == expected, 'Implementation source mismatch: ' + name
            shell_eol_only.append(name)
        local += 1
    required = [line.strip() for line in (BASE / 'soc/bosio_sources.f').read_text().splitlines() if line.strip()]
    required += ['soc/board/' + name for name in (
        'kfb_axi_mig.sv', 'kfb_native_arbiter.sv', 'kfb_video_fifo.sv', 'kfb_video.sv',
        'kfb_hdmi.sv', 'sd_boot.sv', 'sd_reader.sv', 'gen3_kfb_bosio.sv',
        'kfb.xdc', 'sd.xdc', 'peripherals.xdc', 'mig_a.prj')]
    for name in required:
        assert name in manifest and (BASE / name).is_file(), 'Missing frozen build input: ' + name
    validation = json.loads((BASE / 'provenance/validation_summary.json').read_text())
    for check in validation['checks']:
        assert digest(BASE / check['log']) == check['sha256'], check['log']
        assert check['pass_lines'], check['log']
    lru = json.loads((BASE / 'provenance/lru_equivalence.json').read_text())
    for name, expected in lru['logs'].items():
        assert digest(BASE / name) == expected, name
    release = json.loads((BASE / 'provenance/release_manifest.json').read_text())
    assert release['bitstream']['sha256'] == '9bb855886be0ac55d983f9a72ab0e6c23317b7f4fe27e13430cb2de17d7d4e07'
    timing = release['timing_ns']
    assert timing['wns'] >= 0 and timing['whs'] >= 0 and timing['tns'] == timing['ths'] == 0
    if require_tracked:
        tracked = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0'))
        for name in manifest:
            if not name.startswith(EXTERNAL):
                assert (REL / name).as_posix() in tracked, 'Not staged/tracked: ' + name
        for name in tracked:
            if name.startswith('variants/l1-'):
                assert '/soc/board/vendor/hdmi/' not in name, name
                assert '/soc/board/vendor/digilent_rgb2dvi/src/' not in name, name
                assert not name.endswith(('.bit', '.dcp', '.img', '.xci')), name
    result = dict(result='PASS_SOURCE_PUBLICATION_AUDIT', checked_source_files=local,
                  external_source_records=external, required_build_inputs=len(required),
                  shell_line_endings_only=shell_eol_only,
                  stored_rtl_logs=len(validation['checks']), stored_lru_logs=len(lru['logs']),
                  bitstream_sha256=release['bitstream']['sha256'],
                  scope='Preserved sources/evidence only; no synthesis or physical board rerun')
    print(json.dumps(result, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-tracked', action='store_true')
    verify(parser.parse_args().require_tracked)
