"""Extract paired paused/active board runs without including UART transmission."""
import argparse
import json
import math
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('log', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
text = args.log.read_text(encoding='utf-8', errors='replace')
report = {'source': args.log.name, 'conditions': {}}
for label in ('PAUSED', 'ACTIVE'):
    block = re.search(r'(?:^|\n)' + label + r'_BEGIN\r*\n(.*?)\n' + label + r'_END\r*\n', text, re.S)
    assert block, f'Missing complete {label} run'
    assert 'INPUT_BENCH_DONE' in block[1]
    samples = [json.loads(x) for x in re.findall(r'INPUT_SAMPLE (\{[^\r\n]+\})', block[1])]
    assert len(samples) == 41, len(samples)
    metrics = {}
    for name in dict.fromkeys(x['name'] for x in samples):
        rows = [x for x in samples if x['name'] == name]
        metrics[name] = {'n': len(rows)}
        for key in ('accept_ms', 'feedback_ms', 'present_ms'):
            values = sorted(x[key] for x in rows)
            metrics[name][key] = dict(median=statistics.median(values), p95=values[math.ceil(len(values) * .95) - 1], maximum=values[-1])
    report['conditions'][label.lower()] = {'scope': 'Board-local request to response/feedback/final publication. UART, capture and optical latency excluded.', 'samples': samples, 'metrics': metrics}
args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
for condition, run in report['conditions'].items():
    print(condition, {name: round(run['metrics'][name]['present_ms']['median'], 1) for name in ('pointer_move', 'drag_pointer', 'resize_preview', 'move_release', 'resize_release')})
