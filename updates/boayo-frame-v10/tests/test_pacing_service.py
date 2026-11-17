"""Exercise the real worker with mock device latency, independently of wall clock speedups."""
import json,os,socket,subprocess,sys,tempfile,time
from pathlib import Path
binary=Path(sys.argv[1]);reports=[]
for delay in (0,650):
 with tempfile.TemporaryDirectory() as td:
  path=td+'/wm.sock'
  env=dict(os.environ,BOAYO_SOCKET=path,BOAYO_MOCK_RENDER_DELAY_MS=str(delay),BOAYO_PROFILE='1')
  with open(td+'/log','w') as log:
   p=subprocess.Popen([str(binary),'serve','--mock'],env=env,stdout=log,stderr=log)
   try:
    end=time.monotonic()+90
    while not Path(path).exists():
     assert p.poll() is None and time.monotonic()<end;time.sleep(.02)
    s=socket.socket(socket.AF_UNIX);s.settimeout(30);s.connect(path);f=s.makefile('rwb',buffering=0);seq=0
    def call(op,args={},ok=True):
     global seq
     seq+=1;f.write((json.dumps(dict(id=seq,app='pacer-test',op=op,args=args))+'\n').encode())
     r=json.loads(f.readline());assert r['ok']==ok,r;return r.get('result')
    while not call('profile_progress').get('performance',{}).get('render_count',0):
     assert time.monotonic()<end;time.sleep(.1)
    call('set_frame_limit',dict(fps=0),False)
    call('set_frame_limit',dict(fps=501),False)
    assert call('set_frame_limit',dict(fps=2))==2
    call('launch_app',dict(kind='telemetry'))
    rows=[];last=0;end=time.monotonic()+45
    while len(rows)<6:
     r=call('profile_progress');assert r['render_error'] is None and r['app_error'] is None,r
     perf=r['performance'];n=perf['render_count']
     if n!=last:
      last=n;rows.append(perf)
     assert time.monotonic()<end;time.sleep(.03)
    # Ignore first launch/state transition; subsequent starts must include work.
    intervals=[r['frame_interval_ms'] for r in rows[2:]]
    assert min(intervals)>=495,intervals
    # Allow host jitter; old end-to-start pacing needs >= 1150 ms at 650 ms work.
    if delay:assert sum(intervals)/len(intervals)<1050,intervals
    else:assert sum(intervals)/len(intervals)<800,intervals
    assert call('set_frame_limit',dict(fps=4))==4
    call('shutdown');assert p.wait(timeout=30)==0
    reports.append(dict(mock_delay_ms=delay,intervals_ms=intervals))
   finally:
    if p.poll() is None:p.terminate();p.wait(timeout=30)
print(json.dumps(dict(result='PASS_PACING_SERVICE',hardware=False,cases=reports)))
