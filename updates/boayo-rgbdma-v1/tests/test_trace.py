"""Compare actual desktop pixels and composed scene with the uninstrumented binary."""
from pathlib import Path
import base64,hashlib,json,os,re,subprocess,sys
baseline=Path(sys.argv[1]);candidate=Path(sys.argv[2]);out=Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
def run(binary,label,enabled,dma=False):
    directory=out/label;directory.mkdir(exist_ok=True)
    with (directory/'trace.log').open('w') as log:
        env=dict(os.environ,BOAYO_INIT_TRACE=str(int(enabled)),BOAYO_INIT_TRACE_UART='0',BOAYO_TEST_RGB_DMA=str(int(dma)))
        proc=subprocess.Popen([str(binary),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,env=env)
        def request(op,args={}):
            proc.stdin.write(json.dumps(dict(op=op,args=args,app='trace-test'))+'\n');proc.stdin.flush()
            reply=json.loads(proc.stdout.readline());assert reply['ok'],reply
            return reply['result']
        try:
            request('test_desktop',dict(op='tick',now=0))
            if dma:assert request('test_rgb_dma_stats')['calls']>=2,'DMA hook not exercised'
            state=request('get_state')
            pixels={}
            for w in state['windows']:
                raw=base64.b64decode(request('test_surface',dict(window_id=w['window_id'])))
                pixels[str(w['window_id'])]=hashlib.sha256(raw).hexdigest()
            request('test_render',dict(path=str(directory/'frame'),full=True))
            proc.stdin.close();assert proc.wait(timeout=30)==0
        finally:
            if proc.poll() is None:proc.kill();proc.wait()
    return dict(windows=state['windows'],pixels=pixels,scene_sha256=hashlib.sha256((directory/'frame.scene').read_bytes()).hexdigest())
a=run(baseline,'baseline',False)
b=run(candidate,'disabled',False)
c=run(candidate,'enabled',True)
d=run(candidate,'dma',True,True)
assert a==b==c==d,'CPU/DMA changed desktop pixels/state/scene'
assert not (out/'disabled/trace.log').read_text()
log=(out/'enabled/trace.log').read_text()
items=re.findall(r'BOAYO_INITTRACE n=(\d+) event=(\S+) wall_us=(-?\d+) cpu_us=(-?\d+) value=(-?\d+)',log)
assert 20<len(items)<128,len(items)
assert [int(v[0]) for v in items]==list(range(1,len(items)+1))
events=[v[1] for v in items]
ordered=['desktop.begin','window.create.begin','window.create.end','window.configure.begin','window.configure.end',
 'launcher.allocate.begin','surface.clear.begin','surface.clear.end','launcher.allocate.end',
 'rounded.begin','rounded.row','rounded.end','circle.begin','circle.row','circle.end',
 'launcher.text.begin','launcher.text.end','launcher.app.begin','launcher.apps.end','launcher.image.end',
 'surface.publish.begin','surface.scan.row','surface.scan.end','surface.copy.begin','surface.copy.end','surface.publish.end','desktop.end','desktop.scope_exit']
pos=0
for event in ordered:pos=events.index(event,pos)+1
assert all(int(x[2])<=int(y[2]) for x,y in zip(items,items[1:]))
valid=[int(x[3]) for x in items if int(x[3])>=0]
assert valid==sorted(valid)
report=dict(result='PASS_TRACE_AND_PIXEL_EQUIVALENCE',events=len(items),checks=ordered,
 desktop=a,hardware=False,baseline_sha256=hashlib.sha256(baseline.read_bytes()).hexdigest(),
 candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
