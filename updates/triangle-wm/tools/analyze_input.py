"""Summarize board-local request-to-wait_present samples from a UART log."""
import argparse, json, math, re, statistics
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('log', type=Path)
ap.add_argument('output', type=Path)
args = ap.parse_args()
samples = [json.loads(m) for m in re.findall(r'INPUT_SAMPLE (\{[^\r\n]+\})', args.log.read_text(encoding='utf-8', errors='replace'))]
assert samples, 'No input measurements'
groups = {}
for sample in samples:
    groups.setdefault(sample['name'], []).append(sample['present_ms'])
metrics = []
for name, values in groups.items():
    values.sort()
    metrics.append(dict(name=name, n=len(values), median_ms=round(statistics.median(values), 3),
                        p95_ms=round(values[math.ceil(len(values)*.95)-1], 3), max_ms=round(max(values), 3),
                        scope='board control request through wait_present; excludes optical/UART delay'))
report = dict(source=args.log.name, complete='INPUT_BENCH_DONE' in args.log.read_text(encoding='utf-8', errors='replace'), metrics=metrics, samples=samples)
args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(metrics, indent=2))
