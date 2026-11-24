"""Collect controlled board timings without mixing driver or client versions."""
import hashlib, json, statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1]
evidence = root/'evidence'/'factorial'
reports = {p.parent.name: json.loads(p.read_text()) for p in evidence.glob('*/summary.json')}
columns = ['idle_ipc', 'paused_accept', 'active_accept', 'paused_final', 'active_final']
rows = {}
for label, r in reports.items():
    values = {'idle_ipc': r['idle_ipc']['median_ms']}
    for condition in ['paused','active']:
        metrics = r['conditions'][condition]['metrics']
        values[condition+'_accept'] = metrics['acceptance']['median_ms']
        values[condition+'_final'] = metrics['final_point_completion']['median_ms']
    rows[label] = values
    print(label, ' '.join(f'{k}={values[k]:.3f}' for k in columns))
pairs = [
    ('fixed_effect_json','float-json','fixed-json'),
    ('fixed_effect_binary','float-binary','fixed-binary'),
    ('binary_effect_float','float-json','float-binary'),
    ('binary_effect_fixed','fixed-json','fixed-binary'),
    ('optimized_effect','fixed-binary','fixed-binary-optimized'),
]
changes = {name: {k: (rows[after][k]/rows[before][k]-1)*100 for k in columns}
           for name,before,after in pairs if before in rows and after in rows}
report = dict(scope='Same boot, BO_PUBLISH_COPY for every case. One matrix executable; common float diagnostic client/app, only server numeric implementation and socket codec vary. Three 16-move bursts per paused/active condition; idle IPC 2 warmups + 20 samples. Native timings exclude UART/capture. Lower is better.',
              matrix_sha256=hashlib.sha256((evidence/'wm-matrix').read_bytes()).hexdigest(),
              medians_ms=rows, changes_percent=changes, raw=reports)
paced = {}
for label in ['matrix-baseline', 'matrix-optimized']:
    path = evidence/label/'results.json'
    if not path.exists():
        continue
    r = json.loads(path.read_text(encoding='utf-8'))
    (root/'results'/('factorial-paced-'+label+'.json')).write_text(json.dumps(r,indent=2)+'\n')
    values = [x['final_event_to_hdmi_ms'] for x in r['rounds']]
    paced[label] = dict(n=len(values),samples_ms=values,median_ms=statistics.median(values),maximum_ms=max(values),scope=r['scope'])
report['hdmi'] = paced
if len(paced) == 2:
    report['hdmi_change_percent'] = (paced['matrix-optimized']['median_ms']/paced['matrix-baseline']['median_ms']-1)*100
target = root/'results'/'factorial-comparison.json'
target.write_text(json.dumps(report,indent=2)+'\n')
print('CHANGES_PERCENT',json.dumps(changes))
