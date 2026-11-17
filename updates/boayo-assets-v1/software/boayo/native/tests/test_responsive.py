"""A real IPC status request must finish while the mock render worker is delayed."""
import json, os, socket, subprocess, sys, tempfile, time
from pathlib import Path

with tempfile.TemporaryDirectory(prefix='boayo-responsive-') as tmp:
    path = str(Path(tmp) / 'wm.sock')
    env = dict(os.environ, BOAYO_SOCKET=path, BOAYO_MOCK_RENDER_DELAY_MS='3000')
    with open(Path(tmp) / 'log', 'w+') as log:
        proc = subprocess.Popen([sys.argv[1], 'serve', '--mock'], env=env, stdout=log, stderr=log)
        def call(op, args=None, timeout=1):
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(timeout); s.connect(path)
                s.sendall((json.dumps(dict(id=1, app='observer', op=op, args=args or {}))+'\n').encode())
                result = json.loads(s.makefile('rb').readline())
                assert result['ok'], result
                return result['result']
        try:
            deadline = time.monotonic()+30
            while not Path(path).exists():
                assert proc.poll() is None
                assert time.monotonic()<deadline
                time.sleep(.02)
            phases=set(); delays=[]; snapshots=[]
            # The window remains responsive for the entire injected render delay.
            while time.monotonic()<deadline:
                start=time.monotonic(); state=call('get_state'); elapsed=time.monotonic()-start
                phases.add(state['service']['phase'])
                assert state['render_error'] is None, state
                if state['service']['phase']=='composing':
                    delays.append(elapsed); snapshots.append(state['service']['snapshot_age_seconds'])
                if state['output']['commits']>=2: break
                time.sleep(.04)
            else: raise AssertionError('render did not finish')
            assert len(delays)>=20, (len(delays),phases)
            assert max(delays)<.5, delays
            assert max(snapshots)>1.0, snapshots
            assert call('get_button_state')['available'] is False
            assert call('ping')['runtime']=='C++17'
            call('shutdown')
            assert proc.wait(timeout=10)==0
            print(json.dumps(dict(result='PASS',scope='host mock delayed render, not board latency',
                phases=sorted(phases),samples=len(delays),maximum_status_ms=max(delays)*1000,
                injected_render_ms=3000)))
        finally:
            if proc.poll() is None: proc.terminate(); proc.wait(timeout=15)
            if proc.returncode: log.seek(0); print(log.read(),file=sys.stderr)
