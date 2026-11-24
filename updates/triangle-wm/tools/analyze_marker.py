"""Compare operation counts from the same SD image, never hardware latency."""
import argparse
import json
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('image_folder', type=Path)
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
reports = {}
samples = {}
for mode, folder in [('tile', 'focused-boot'), ('outline', 'reference-boot')]:
    root = args.image_folder / folder
    reports[mode] = json.loads((root / 'result.json').read_text())
    assert reports[mode]['runtime_preview'] == mode
    assert not reports[mode]['hardware']
    samples[mode] = json.loads((root / 'marker-samples.json').read_text())
manifest = json.loads((args.image_folder / 'manifest.json').read_text())
assert reports['tile']['image_sha256'] == reports['outline']['image_sha256'] == manifest['sha256']
comparison = {}
for mode, rows in samples.items():
    comparison[mode] = {}
    for row in rows:
        first, last = row['before']['last_render'], row['moves'][-1]['render']
        delta = {field: last[field] - first[field] for field in
                 ['footprint_calls_total', 'plan_builds_total', 'run_submissions_total']}
        delta.update(samples=len(row['moves']),
                     release_publications=row['after']['publications'] - row['before_release']['publications'],
                     release_plan_builds=row['after']['last_render']['plan_builds_total'] - last['plan_builds_total'])
        if mode == 'tile':
            assert all(delta[field] == 0 for field in ['plan_builds_total', 'run_submissions_total'])
            if row['action']=='move':assert delta['footprint_calls_total']==0
            assert all(move['render']['ui_masks_us'] == 0 for move in row['moves'])
            assert delta['release_publications'] == 1 and delta['release_plan_builds'] == 1
            assert row['release']['result']['transform']['state'] == 'complete'
            delta['max_changed_directory_entries'] = max(move['render']['marker']['changed_entries'] for move in row['moves'])
            delta['motion_ui_mask_work'] = 'none'
            delta['preview_mode'] = row['moves'][0]['preview']['mode']
        else:
            assert delta['footprint_calls_total'] == delta['samples']
        comparison[mode][row['action']] = delta
result = dict(scope='Operation counts on actual RV32 executables in QEMU, mock BOSIO; same final SD image. No physical board connection or latency measurement.',
              hardware_tested=False, feedback_target_ms=300, feedback_target_hardware_verified=False,
              image_sha256=manifest['sha256'], reference='Retained optimized thin-cell outline, TRIANGLE_PREVIEW=outline. Not the historical November 19 baseline.',
              comparison=comparison, latency='Unmeasured on hardware. Raw emulator times are diagnostic only.',
              validation='Host ASan/UBSan marker, overlay, input, mailbox, binary/JSON transport, latest-content coalescing, ABI and pre-publication rollback suites passed.')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
