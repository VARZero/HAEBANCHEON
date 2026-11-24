"""Board factorial comparison: retain COPY driver, common app and input client."""
import argparse, json, os, re, statistics, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('cases', nargs='*', default=['float-json', 'fixed-binary', 'fixed-json', 'float-binary'])
ap.add_argument('--dry-run', action='store_true')
a = ap.parse_args()
cases = {
    'float-json': ('float','json',0,0,0),
    'fixed-binary': ('fixed','binary',0,0,0),
    'fixed-json': ('fixed','json',0,0,0),
    'float-binary': ('float','binary',0,0,0),
    'fixed-binary-codec': ('fixed','binary',1,0,0),
    'fixed-binary-checkpoint': ('fixed','binary',0,1,0),
    'fixed-binary-math': ('fixed','binary',0,0,1),
    'fixed-binary-optimized': ('fixed','binary',1,1,1),
}
for label in a.cases:
    numeric,wire,codec,checkpoint,math = cases[label]
    output = root/'evidence'/'factorial'/label
    output.mkdir(parents=True, exist_ok=True)
    cmd = ('kill -TERM $(cat /tmp/triangle-app.pid) 2>/dev/null; '
           'kill -CONT $(cat /tmp/triangle-app.pid) 2>/dev/null; sleep 2; '
           'kill -TERM $(cat /tmp/triangle-wm.pid) 2>/dev/null; sleep 2; '
           f'export TRIANGLE_NUMERIC={numeric} TRIANGLE_WIRE={wire} TRIANGLE_WIRE_OPT={codec} '
           f'TRIANGLE_CHECKPOINT_OPT={checkpoint} TRIANGLE_MATH_OPT={math} TRIANGLE_PUBLISH_COPY=1; '
           '/tmp/wm-matrix > /tmp/matrix-wm.log 2>&1 & echo $! > /tmp/triangle-wm.pid; sleep 4; '
           '/tmp/wm-matrix --input --ipc-bench; '
           '/tmp/wm-matrix --app --rgb332 --demo > /tmp/matrix-app.log 2>&1 & echo $! > /tmp/triangle-app.pid; '
           'sleep 12; kill -STOP $(cat /tmp/triangle-app.pid); echo PAUSED_BEGIN; '
           '/tmp/wm-matrix --input --stream-bench-async; echo PAUSED_END; '
           'kill -CONT $(cat /tmp/triangle-app.pid); sleep 3; echo ACTIVE_BEGIN; '
           '/tmp/wm-matrix --input --stream-bench-async; echo ACTIVE_END')
    assert len(cmd.encode()) <= 960, len(cmd)
    if a.dry_run:
        print(label, len(cmd.encode()), flush=True)
        continue
    print('MATRIX_BEGIN',label,flush=True)
    subprocess.run([sys.executable,str(root/'tools'/'uart.py'),cmd,'--timeout','300','--output',str(output)],check=True)
    log = max(output.glob('TRI_*.log'), key=lambda p:p.stat().st_mtime)
    result = output/'summary.json'
    subprocess.run([sys.executable,str(root/'tools'/'analyze_input_mailbox.py'),str(log),str(result)],check=True)
    report = json.loads(result.read_text())
    report['configuration'] = dict(numeric=numeric,wire=wire,codec=codec,checkpoint=checkpoint,math_cache=math,driver='BO_PUBLISH_COPY',common_clients=True)
    ipc = json.loads(re.search(r'(?:^|\n)IPC_BENCH (\{[^\r\n]+\})',log.read_text(errors='replace'))[1])
    report['idle_ipc'] = ipc | dict(median_ms=statistics.median(ipc['roundtrip_ms']))
    result.write_text(json.dumps(report,indent=2)+'\n')
    print('MATRIX_END',label,'idle_ipc_ms',report['idle_ipc']['median_ms'],flush=True)
