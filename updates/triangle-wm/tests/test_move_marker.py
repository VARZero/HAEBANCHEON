"""Socket/state/pixel checks for frozen previews and one final transform."""
import json,mmap,os,select,socket,subprocess,sys,tempfile
import binary_test_wire as wire

class Client:
    def __init__(self,path):
        self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path);self.f=self.s.makefile('rb');self.seq=0
    def send(self,op,args=None):
        self.seq+=1;self.s.sendall(wire.encode(dict(id=self.seq,op=op,args=args or {})))
    def receive(self,ok=True):
        r=wire.read(self.f);assert r['ok']==ok,r;return r.get('result',r)
    def call(self,op,args=None,ok=True):self.send(op,args);return self.receive(ok)
    def do(self,op,args=None):
        r=self.call(op,args)
        if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
        return r
    def close(self):self.f.close();self.s.close()

class Fixture:
    def __init__(self,folder,name,fail=None):
        path=folder+'/'+name;env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_INPUT':'1','TRIANGLE_PREVIEW':'tile'}
        if fail:env['TRIANGLE_MOCK_FAIL_TRANSFORM']=fail
        self.p=subprocess.Popen([sys.argv[1],'--mock'],env=env,stdout=subprocess.PIPE,text=True)
        assert self.p.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
        self.app=Client(path);self.ctl=Client(path+'.control')
        self.wid=self.app.do('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32))['window_id']
        info=self.app.call('create_surface',dict(window_id=self.wid,format='RGB332'));fd=os.open(info['path'],os.O_RDWR)
        self.pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd);self.image=bytearray(bytes(range(256))*4);self.seq=0
        self.pixels[:1024]=self.image;self.commit([[0,0,32,32]],wait=True)
        self.ctl.do('input_visibility',dict(visible=False));self.ctl.do('input_cancel',dict(hide=True))
    def commit(self,damage,wait=False):
        self.seq+=1;r=self.app.call('commit_surface',dict(window_id=self.wid,slot=0,sequence=self.seq,damage=damage))
        if wait:self.app.call('wait_present',dict(generation=r['generation']))
        return r
    def state(self):return self.ctl.call('get_state')
    def status(self):return self.ctl.call('input_status')
    def hashes(self):return self.ctl.call('snapshot')['tile_hashes']
    def handle(self,kind):return next(c for c in self.status()['controls'] if c['action']==kind)
    def press(self,kind):
        c=self.handle(kind);self.ctl.do('input_button',dict(x=c['x'],y=c['y'],pressed=True));return c
    def release(self,x,y):
        r=self.ctl.call('input_button',dict(x=x,y=y,pressed=False));done=self.ctl.call('wait_present',dict(generation=r['generation']));return r,done
    def close(self):
        self.pixels.close();self.app.close()
        if self.p.poll() is None:self.ctl.call('shutdown')
        self.ctl.close();assert self.p.wait(timeout=10)==0

def geometry(f):return {k:f.state()['windows'][0][k] for k in ['azimuth','elevation','width_deg','height_deg']}
with tempfile.TemporaryDirectory() as folder:
    f=Fixture(folder,'main');ref=None
    try:
        original=geometry(f);c=f.press('move');before=f.state();baseline=f.hashes();old_tile=f.status()['preview']['tile']
        counts=before['performance']['last_render'];plans=counts['plan_builds_total'];footprints=counts['footprint_calls_total'];runs=counts['run_submissions_total']
        for dx,dy in [(25,0),(100,0),(-100,20),(80,-20),(140,0)]:
            pubs=f.state()['performance']['publications'];f.ctl.do('input_move',dict(x=c['x']+dx,y=c['y']+dy))
            current=f.hashes();tile=f.status()['preview']['tile'];changed={i for i,(a,b) in enumerate(zip(baseline,current)) if a!=b}
            assert changed<={old_tile,tile},changed
            state=f.state();r=state['performance']['last_render'];assert geometry(f)==original
            assert r['marker']['colored_cells_per_tile']==1024 and r['marker']['payload_ops']==0 and r['marker']['payload_builds']==0
            assert r['plan_builds_total']==plans and r['footprint_calls_total']==footprints and r['run_submissions_total']==runs
            if state['performance']['publications']>pubs:assert r['ui_masks_us']==0 and r['projected_tiles']==0 and r['plan_tiles']==0,r
            baseline=current;old_tile=tile
        # Invalid mutation must not cancel or alter the active capture.
        f.ctl.call('configure_window',dict(window_id=f.wid,width_deg=-1),False);assert f.status()['preview']['active']
        # Queue overlapping latest-content updates; keep the frozen frame intact.
        frozen=f.hashes();publications=f.state()['performance']['publications']
        for color in [11,77,219]:
            for y in range(4,12):f.image[y*32+4:y*32+12]=bytes([color])*8
            f.pixels[:1024]=f.image;last=f.commit([[4,4,8,8]])
            assert f.hashes()==frozen and f.state()['performance']['publications']==publications
        f.app.send('wait_present',dict(generation=last['generation']))
        f.ctl.do('input_move',dict(x=c['x']+120,y=c['y']-20))
        assert not select.select([f.app.s],[],[],.03)[0],'preview acknowledged undisplayed app content'
        before_commit=f.state()['performance'];r,done=f.release(c['x']+120,c['y']-20)
        assert r['commit_pending'] and done['transform']['state']=='complete' and not done['preview_only'],(r,done)
        f.app.receive();after=f.state();assert after['performance']['publications']==before_commit['publications']+1
        assert after['performance']['shell_publications']==before_commit['shell_publications']
        assert after['performance']['last_render']['plan_builds_total']==plans+1
        assert not f.status()['preview']['active'] and geometry(f)!=original
        # Compare with a full reconstruction of the latest image and geometry.
        ref=Fixture(folder,'reference');ref.ctl.do('configure_window',dict(window_id=ref.wid,**geometry(f)))
        ref.image[:]=f.image;ref.pixels[:1024]=f.image;ref.commit([[0,0,32,32]],wait=True)
        ref.ctl.do('input_move',dict(x=c['x']+120,y=c['y']-20));assert f.hashes()==ref.hashes(),'final pixels differ from full rebuild'
        ref.close();ref=None
        # Cancel restores exactly; resize uses a marker, then one scale rebuild.
        f.ctl.do('input_cancel',dict(hide=True));old=f.hashes();old_geometry=geometry(f);c=f.press('move');f.ctl.do('input_move',dict(x=c['x']-100,y=c['y']));f.ctl.do('input_cancel',dict(hide=True))
        assert f.hashes()==old and geometry(f)==old_geometry
        c=f.press('resize');old_geometry=geometry(f);plans=f.state()['performance']['last_render']['plan_builds_total']
        previous=f.hashes();old_rim=set(f.status()['preview']['visible_filled_tiles'])
        for dx in [20,50,80]:
            f.ctl.do('input_move',dict(x=min(1279,c['x']+dx),y=min(719,c['y']+20)));assert geometry(f)==old_geometry
            preview=f.status()['preview'];assert preview['mode']=='solid-resize' and len(preview['filled_tiles'])>4 and preview['payload_ops']==0
            current=f.hashes();rim=set(preview['visible_filled_tiles']);changed={i for i,(a,b) in enumerate(zip(previous,current)) if a!=b}
            assert changed<=old_rim|rim,changed
            previous=current;old_rim=rim
        before_commit=f.state()['performance']['publications'];r,done=f.release(min(1279,c['x']+40),min(719,c['y']+25))
        assert done['transform']['state']=='complete' and geometry(f)['width_deg']>old_geometry['width_deg']
        assert f.state()['performance']['publications']==before_commit+1
        assert f.state()['performance']['last_render']['plan_builds_total']==plans+1
        # External scene mutation cancels the frozen preview safely.
        f.press('move');f.ctl.do('configure_window',dict(window_id=f.wid,azimuth=0));assert not f.status()['preview']['active'] and not f.status()['pressed']
        print('PASS_MARKER_SOCKET: one changed tile pair, frozen geometry/content, latest damage, app completion isolation, one final rebuild, cancel, resize, scene barriers')
    finally:
        if ref:ref.close()
        f.close()
    for failure in ['preflight','publish']:
        f=Fixture(folder,'failure-'+failure,failure)
        try:
            before=geometry(f);old=f.hashes();c=f.press('move');f.ctl.do('input_move',dict(x=c['x']+140,y=c['y']))
            r,done=f.release(c['x']+140,c['y']);assert done['transform']['state']=='failed' and done['transform']['error'],done
            assert geometry(f)==before;f.ctl.do('input_cancel',dict(hide=True));assert f.hashes()==old,'failed transform lost old content'
            assert not f.status()['preview']['active'];print('PASS_TRANSFORM_ROLLBACK',failure)
        finally:f.close()
