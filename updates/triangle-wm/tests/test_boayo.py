"""Exercise the real socket/memfd ABI and compare partial rendering with full rebuilds."""
import json, mmap, os, socket, subprocess, sys, tempfile, time, struct

class Client:
    def __init__(self, path):
        self.sock=socket.socket(socket.AF_UNIX); self.sock.settimeout(10); self.sock.connect(path)
        self.file=self.sock.makefile('rwb',buffering=0); self.seq=0
    def call(self, op, args=None, ok=True):
        self.seq+=1
        self.file.write((json.dumps(dict(id=self.seq,app='same-name',op=op,args=args or {}))+'\n').encode())
        reply=json.loads(self.file.readline()); assert reply['id']==self.seq,reply
        assert reply['ok']==ok,reply
        return reply.get('result',reply)
    def wait(self,result): return self.call('wait_present',{'generation':result['generation']})
    def close(self): self.file.close(); self.sock.close()

class Server:
    def __init__(self,path,format="RGB24"):
        self.format=format
        self.proc=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path},stdout=subprocess.PIPE,text=True)
        assert self.proc.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
        self.path=path; self.app=Client(path); self.ctl=Client(path+'.control')
    def close(self):
        self.app.close(); self.ctl.call('shutdown'); self.ctl.close()
        assert self.proc.wait(timeout=10)==0
    def hashes(self): return self.ctl.call('snapshot')['tile_hashes']
    def state(self): return self.ctl.call('get_state')

class Window:
    def __init__(self,server,config=None):
        self.server=server; self.config={**dict(surface_width=96,surface_height=64,width_deg=20,height_deg=14,background_index=84),**(config or {})}
        result=server.app.call('create_window',self.config); self.id=result['window_id']; server.app.wait(result)
        self.info=server.app.call('create_surface',{'window_id':self.id,'format':server.format})
        assert server.app.call('create_surface',{'window_id':self.id})['format']==server.format
        server.app.call('create_surface',{'window_id':self.id,'format':'invalid'},ok=False)
        server.app.call('create_surface',{'window_id':self.id,'format':'RGB24' if server.format=='XRGB8888' else 'XRGB8888'},ok=False)
        fd=os.open(self.info['path'],os.O_RDWR); self.map=mmap.mmap(fd,self.info['slot_bytes']*2); os.close(fd)
        self.width=self.info['width']; self.height=self.info['height']; self.pixels=bytearray([72,182,0])*(self.width*self.height); self.seq=0
    def paint(self,rect,rgb):
        x,y,w,h=rect
        if self.info['format']=='RGB332':
            r,g,b=rgb;rgb=[(r>>5)*255//7,(g>>5)*255//7,(b>>6)*255//3]
        for row in range(y,y+h): self.pixels[(row*self.width+x)*3:(row*self.width+x+w)*3]=bytes(rgb)*w
    def commit(self,rects=None):
        rects=rects or [[0,0,self.width,self.height]]; slot=self.seq%2
        for x,y,w,h in rects:
            for row in range(y,y+h):
                start=(row*self.width+x)*3; target=slot*self.info['slot_bytes']+start
                if self.info['format']=='RGB24': self.map[target:target+w*3]=self.pixels[start:start+w*3]
                elif self.info['format']=='RGB332':
                    target=slot*self.info['slot_bytes']+row*self.width+x
                    packed=bytearray()
                    for col in range(w):
                        r,g,b=self.pixels[start+col*3:start+col*3+3];packed.append((r&224)|((g>>3)&28)|(b>>6))
                    self.map[target:target+w]=packed
                else:
                    target=slot*self.info['slot_bytes']+(row*self.width+x)*4
                    packed=bytearray()
                    for col in range(w):
                        r,g,b=self.pixels[start+col*3:start+col*3+3];packed.extend(struct.pack('<I',(r<<16)|(g<<8)|b))
                    self.map[target:target+w*4]=packed
        self.seq+=1
        r=self.server.app.call('commit_surface',dict(window_id=self.id,sequence=self.seq,slot=slot,damage=rects)); self.server.app.wait(r); return r
    def configure(self,**config):
        r=self.server.ctl.call('configure_window',dict(window_id=self.id,**config)); self.server.ctl.wait(r); self.config.update(config)
    def close(self):
        r=self.server.ctl.call('destroy_window',{'window_id':self.id}); self.server.ctl.wait(r); self.map.close()

with tempfile.TemporaryDirectory(prefix='triangle-api-') as tmp:
    # Do not unlink files or an unrelated service's live socket during startup.
    blocked=tmp+'/existing'; payload=b'keep this file'
    with open(blocked,'wb') as f: f.write(payload)
    for kind in ('file','live-socket'):
        if kind=='live-socket':
            os.unlink(blocked); listener=socket.socket(socket.AF_UNIX); listener.bind(blocked); listener.listen(4)
        result=subprocess.run([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':blocked},capture_output=True,timeout=10)
        assert result.returncode!=0 and os.path.exists(blocked)
        if kind=='file':
            with open(blocked,'rb') as f: assert f.read()==payload
        else: listener.close()
    # Recover a dead service's socket after the live-owner check rejects it.
    stale=Server(blocked); stale.close()
    main=Server(tmp+'/main.sock',os.environ.get('TEST_SURFACE_FORMAT','RGB24')); reference=Server(tmp+'/ref.sock'); reference_windows=[]
    a=Window(main,{'title':'quoted "title" and \\ path \uD55C\uAE00'}); a.paint([8,8,70,40],[220,40,90]); a.commit()
    state=main.state();initial=state['performance']['last_render']['projected_tiles'];assert initial>0
    assert state['windows'][0]['title']==a.config['title']
    report=state['performance']['last_render'];assert report['request_profile']['operation']=='commit_surface'
    assert report['request_profile']['dispatch_us']>=report['request_profile']['snapshot_us']
    # Diagnostic queries preserve generation and completed-frame statistics.
    again=main.state();assert again['generation']==state['generation'] and again['performance']['last_render']==report
    windows=[a]
    def compare():
        for w in reference_windows: w.close()
        reference_windows.clear()
        for w in windows:
            r=Window(reference,w.config); r.pixels[:]=w.pixels; r.commit(); reference_windows.append(r)
        assert main.hashes()==reference.hashes(),'incremental scene differs from full rebuild'
    compare()
    # Sparse DMA staging changes only the selected source rows/columns.
    a.paint([20,20,8,8],[255,255,255]);a.commit([[20,20,8,8]])
    perf=main.state()['performance']['last_render']
    if os.environ.get('TRIANGLE_SPARSE_UPLOAD','0')!='0':
        assert perf['texture_upload_bytes']==256 and perf['texture_scatter_bytes']==256,perf
    compare()
    # Sparse rectangles, bank reuse and changing slots.
    for n in range(4):
        rects=[[10,10,3,4],[80,48,3,4]]
        for rect in rects: a.paint(rect,[n*40,220,70])
        a.commit(rects)
        assert main.state()['performance']['last_render']['projected_tiles']<initial
        compare()
    # Commit snapshots are immutable even if the released slot is immediately changed.
    saved=main.hashes(); a.map[:]=b'\xff'*len(a.map)
    a.configure(azimuth=5,elevation=2); compare()
    a.configure(azimuth=0,elevation=0); assert main.hashes()==saved
    for width,height in [(27,19),(12,10),(20,14)]:
        a.configure(width_deg=width,height_deg=height); compare()
    # Same app name does not confer ownership, including management and shared buffers.
    stranger=Client(main.path)
    for op in ['create_surface','commit_surface','configure_window','destroy_window','raise_window']:
        stranger.call(op,{'window_id':a.id},ok=False)
    stranger.call('shutdown',ok=False); stranger.call('view_set',ok=False); stranger.close()
    # Validate the complete damage list before applying any pixels or consuming sequence.
    gen=main.state()['generation']; before=main.hashes()
    for args in [dict(slot=0,sequence=a.seq+1,damage=[[0,0,1,1],[96,0,1,1]]),
                 dict(slot=2**32,sequence=a.seq+1,damage=[[0,0,1,1]]),
                 dict(slot=0,sequence=a.seq,damage=[[0,0,1,1]]),
                 dict(slot=0,sequence=a.seq+1,damage=[[2**32,0,1,1]])]:
        main.app.call('commit_surface',dict(window_id=a.id,**args),ok=False)
    main.ctl.call('configure_window',dict(window_id=a.id,azimuth=10,width_deg=0),ok=False)
    main.ctl.call('configure_window',dict(window_id=a.id,surface_width=128),ok=False)
    main.app.call('wait_present',{'generation':gen+1},ok=False)
    assert main.hashes()==before and main.state()['generation']==gen
    # No-op commits consume their sequence but do not create a generation or projection.
    noop=a.commit(); assert not noop['changed'] and noop['generation']==gen
    # Cover an updated window, move/resize the covering content, then expose the changed pixels.
    saved_format=main.format; main.format='XRGB8888' if saved_format=='RGB332' else 'RGB332'
    b=Window(main,{'azimuth':4,'elevation':2}); main.format=saved_format
    b.paint([0,0,96,64],[24,170,90]); b.commit(); windows.append(b); compare()
    a.paint([12,12,8,8],[250,250,250]); a.commit([[12,12,8,8]]); compare()
    b.configure(azimuth=-4,width_deg=24); compare()
    b.close(); windows.remove(b); compare()
    # Fully hidden movement defers planning; exposure must rebuild current pixels.
    b=Window(main,{'azimuth':0,'elevation':0,'width_deg':60,'height_deg':45})
    b.paint([0,0,96,64],[24,170,90]); b.commit(); windows.append(b); compare()
    a.configure(azimuth=1.125,elevation=-0.375); compare()
    a.paint([8,8,6,6],[255,255,255]); a.commit([[8,8,6,6]]); compare()
    b.close(); windows.remove(b); compare()
    a.configure(azimuth=0,elevation=0); compare()
    # Three resident sources: offset sampling, capacity eviction and reload.
    b=Window(main,{'azimuth':6,'elevation':3});b.paint([0,0,96,64],[15,180,60]);b.commit();windows.append(b)
    c=Window(main,{'azimuth':-7,'elevation':-3});c.paint([0,0,96,64],[210,20,120]);c.commit();windows.append(c)
    observed_eviction=main.state()['performance']['last_render'].get('texture_evictions',0)>0;compare()
    for position in [-9,-4,-9]:
        c.configure(azimuth=position)
        perf=main.state()['performance']['last_render'];observed_eviction |= perf.get('texture_evictions',0)>0
        if 'TRIANGLE_TEXTURE_WORDS' not in os.environ: assert perf.get('texture_upload_bytes',0)==0,perf
        compare()
        a.paint([20,20,8,8],[255,255,255] if position==-9 else [0,0,0]);a.commit([[20,20,8,8]]);observed_eviction |= main.state()['performance']['last_render'].get('texture_evictions',0)>0;compare()
    if os.environ.get('TRIANGLE_TEXTURE_WORDS')=='16384':assert observed_eviction,'eviction path was not exercised'
    c.close();windows.remove(c);b.close();windows.remove(b);compare()
    # Camera updates do not advance scene generations.
    gen=main.state()['generation']; main.ctl.call('view_set',{'yaw':5,'pitch':2}); assert main.state()['generation']==gen
    main.ctl.call('view_set',{'yaw':0,'pitch':0}); compare()
    # Disconnect cleanup releases only that application's windows.
    other=Client(main.path); other_window=other.call('create_window',{'title':'other'})['window_id']; other.close()
    for _ in range(100):
        ids=[w['window_id'] for w in main.state()['windows']]
        if other_window not in ids: break
        time.sleep(.01)
    assert ids==[a.id],ids
    a.close(); windows.clear(); compare()
    main.close(); reference.close()
print('PASS_BOAYO_ABI snapshot ownership invalid-request-atomicity sequence sparse-damage resize overlap disconnect pose')
