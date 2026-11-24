import binary_test_wire
"""Socket-level replacement, completion, button barriers and content fairness."""
import json, mmap, os, signal, socket, subprocess, sys, tempfile

class Client:
    def __init__(self,path):
        self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(30);self.s.connect(path)
        self.f=self.s.makefile('rb');self.seq=0
    def encode(self,op,args=None):
        self.seq+=1
        return binary_test_wire.encode(dict(id=self.seq,op=op,args=args or {}))
    def receive(self,ok=True):
        r=binary_test_wire.read(self.f);assert r['ok']==ok,r
        return r.get('result',r)
    def call(self,op,args=None,ok=True):
        self.s.sendall(self.encode(op,args));return self.receive(ok)
    def do(self,op,args=None):
        r=self.call(op,args)
        if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
        return r
    def close(self):self.f.close();self.s.close()

with tempfile.TemporaryDirectory() as folder:
    path=folder+'/wm'
    proc=subprocess.Popen([sys.argv[1],'--mock'],env={**os.environ,'BOAYO_SOCKET':path},stdout=subprocess.PIPE,text=True)
    try:
        assert proc.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
        app=Client(path);ctl=Client(path+'.control')
        wid=app.do('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32))['window_id']
        info=app.call('create_surface',dict(window_id=wid,format='RGB332'))
        fd=os.open(info['path'],os.O_RDWR);pixels=mmap.mmap(fd,info['slot_bytes']*2);os.close(fd)
        pixels[:1024]=bytes(range(256))*4
        app.do('commit_surface',dict(window_id=wid,slot=0,sequence=1,damage=[[0,0,32,32]]))
        ctl.do('input_move',dict(x=700,y=350));expected=ctl.call('snapshot')['tile_hashes']
        ctl.do('input_cancel',dict(hide=True))
        app.call('input_submit',dict(x=1,y=1),False)
        def stopped_batch(client,requests):
            os.kill(proc.pid,signal.SIGSTOP)
            try:client.s.sendall(b''.join(client.encode(op,a) for op,a in requests))
            finally:os.kill(proc.pid,signal.SIGCONT)
            return [client.receive() for _ in requests]
        requests=[('input_submit',dict(x=520+i*12,y=350+(i%3)*10)) for i in range(16)]
        replies=stopped_batch(ctl,requests)
        assert [r['accepted_ticket'] for r in replies]==list(range(1,17))
        assert replies[0]['visible_ticket']<replies[0]['accepted_ticket'],'acceptance falsely claimed display completion'
        progress=ctl.call('wait_input',dict(ticket=16))
        assert progress['visible_ticket']==16 and progress['coalesced_moves']>=15,progress
        assert (progress['visible_x'],progress['visible_y'])==(700,350)
        assert ctl.call('snapshot')['tile_hashes']==expected,'latest pointer pixels differ from direct input'
        ctl.call('wait_input',dict(ticket=17),False)
        ctl.call('input_submit',dict(x=1280,y=0),False)
        assert ctl.call('input_progress')['accepted_ticket']==16
        # Pending coordinates cannot cross press/release barriers or replay later.
        handle=next(c for c in ctl.call('input_status')['controls'] if c['action']=='move')
        x,y=handle['x'],handle['y']
        rs=stopped_batch(ctl,[('input_submit',dict(x=x,y=y)),('input_button',dict(x=x,y=y,pressed=True))])
        ctl.call('wait_present',dict(generation=rs[-1]['generation']))
        rival=Client(path+'.control');rival.call('input_submit',dict(x=30,y=30),False);rival.close()
        rs=stopped_batch(ctl,[('input_submit',dict(x=x+20,y=y)),('input_submit',dict(x=x+40,y=y)),('input_button',dict(x=x+40,y=y,pressed=False))])
        assert rs[-1]['applied']
        ctl.call('wait_present',dict(generation=rs[-1]['generation']))
        state=ctl.call('input_status');assert not state['pressed'] and state['x']==x+40 and not state['preview']['active']
        assert ctl.call('get_state')['windows'][0]['azimuth']!=0
        ctl.do('configure_window',dict(window_id=wid,azimuth=0,elevation=0))
        # Content completion must never be acknowledged by an old-content pointer
        # publication. Keep alternating bursts so a content batch must make progress.
        prior=ctl.call('input_progress')['priority_publications']
        for sequence in range(2,6):
            pixels[:1024]=bytes([sequence*37])*1024
            os.kill(proc.pid,signal.SIGSTOP)
            try:
                app.s.sendall(app.encode('commit_surface',dict(window_id=wid,slot=0,sequence=sequence*2,damage=[[0,0,32,32]])))
                ctl.s.sendall(b''.join(ctl.encode(op,a) for op,a in requests))
            finally:os.kill(proc.pid,signal.SIGCONT)
            commit=app.receive();rs=[ctl.receive() for _ in requests]
            ctl.call('wait_input',dict(ticket=rs[-1]['accepted_ticket']))
            app.call('wait_present',dict(generation=commit['generation']))
            ctl.do('input_cancel',dict(hide=True));image=ctl.call('snapshot')['tile_hashes']
            # Republishing the exact same app state cannot change the restored image.
            same=app.do('commit_surface',dict(window_id=wid,slot=0,sequence=sequence*2+1,damage=[[0,0,32,32]]))
            assert not same['changed'] and ctl.call('snapshot')['tile_hashes']==image
        assert ctl.call('input_progress')['priority_publications']>prior
        # Hidden-dot coordinates complete without painting; logical input remains live.
        ctl.do('input_visibility',dict(visible=False));ctl.do('input_move',dict(x=640,y=360))
        r=ctl.call('input_submit',dict(x=650,y=360));p=ctl.call('wait_input',dict(ticket=r['accepted_ticket']))
        assert p['visible_x']==650 and not ctl.call('input_status')['display_enabled']
        handle=next(c for c in ctl.call('input_status')['controls'] if c['action']=='move')
        ctl.do('input_button',dict(x=handle['x'],y=handle['y'],pressed=True))
        ctl.s.sendall(ctl.encode('input_submit',dict(x=handle['x']+30,y=handle['y'])))
        ctl.close();ctl=Client(path+'.control')
        # A normal request drives disconnect cleanup before observing final state.
        for _ in range(8):
            if not ctl.call('input_status')['pressed']:break
        assert not ctl.call('input_status')['pressed']
        pixels.close();ctl.close();app.close()
        print('PASS_INPUT_MAILBOX: latest-only, distinct acceptance/completion, exact pixels, ordered buttons, capture ownership, content progress, hidden dot, disconnect')
    finally:
        proc.terminate();proc.wait(timeout=10)
        assert proc.returncode==0,proc.returncode
