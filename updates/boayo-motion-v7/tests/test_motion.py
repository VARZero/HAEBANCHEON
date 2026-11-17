"""Exercise actual common-caption input, mixed view pose and scene completion."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile,time,socket,pty,select,termios,signal
binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
checks=[]
class Client:
 def __init__(self,path,name):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(90);self.s.connect(path);self.f=self.s.makefile('rwb',buffering=0);self.name=name;self.n=0
 def call(self,op,args={},fail=False):
  self.n+=1;self.f.write((json.dumps(dict(id=self.n,op=op,args=args,app=self.name))+'\n').encode());r=json.loads(self.f.readline());assert r['id']==self.n,r
  if fail:assert not r['ok'],r;return r
  assert r['ok'],r;return r['result']
 def close(self):self.f.close();self.s.close()
with tempfile.TemporaryDirectory() as tmp:
 path=str(Path(tmp)/'wm.sock');env=dict(os.environ,BOAYO_SOCKET=path,BOAYO_INIT_TRACE='0')
 with (out/'server.log').open('w') as log:
  proc=subprocess.Popen([str(binary),'serve','--mock'],env=env,stdout=log,stderr=log)
  try:
   end=time.monotonic()+60
   while not Path(path).exists():
    assert proc.poll() is None and time.monotonic()<end,'server startup';time.sleep(.05)
   c=Client(path,'motion-test');other=Client(path,'observer')
   def wait(token):
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
     s=other.call('get_state');assert s['render_error'] is None and s['app_error'] is None,s
     if s['performance']['rendered_generation']>=token:
      progress=other.call('motion_progress');assert progress['rendered_generation']>=token and len(json.dumps(progress))<256,progress
      return s
     time.sleep(.03)
    raise AssertionError(('output timeout',s))
   c.call('set_frame_limit',dict(fps=60))
   survivor=c.call('launch_app',dict(kind='sdk-pulse',azimuth=22,elevation=3))
   saved=dict(yaw=7,pitch=-3,roll=11);c.call('view_set',saved)
   initial=wait(c.call('sync_scene'));before={w['window_id'] for w in initial['windows']}
   r=c.call('motion_start');ids=r['windows'];s=wait(r['target_generation'])
   initial_geometry={w['window_id']:{k:w[k] for k in ('azimuth','elevation','width_deg','height_deg')} for w in s['windows'] if w['window_id'] in ids}
   c.call('motion_start',fail=True);other.call('motion_step',dict(step=0),fail=True);c.call('motion_step',dict(step=3),fail=True)
   duplicate=subprocess.run([str(binary),'motion','1','0'],env=env,capture_output=True,text=True,timeout=30)
   assert duplicate.returncode!=0 and other.call('get_state')['motion']['active'],'duplicate CLI stopped another owner'
   checks.append('duplicate start, wrong owner and out-of-order step rejected')
   stages=[]
   for i in range(25):
    r=c.call('motion_step',dict(step=i));s=wait(r['target_generation']);ws={w['window_id']:w for w in s['windows']}
    assert s['performance']['rendered_generation']>=r['target_generation']
    assert not s['pointer']['buttons'],s['pointer']
    if i in (7,8,20):assert all(abs(s['view'][k])>0 for k in ('yaw','pitch','roll')),s['view']
    if i in (10,11,12):
     g=initial_geometry[ids[0]];da,de={10:(-10,4),11:(10,-4),12:(0,0)}[i]
     assert abs(ws[ids[0]]['azimuth']-g['azimuth']-da)<1e-7
     assert abs(ws[ids[0]]['elevation']-g['elevation']-de)<1e-7
    if i in (13,14):
     g=initial_geometry[ids[0]];dw,dh=(6,4) if i==13 else (0,0)
     assert abs(ws[ids[0]]['width_deg']-g['width_deg']-dw)<1e-7
     assert abs(ws[ids[0]]['height_deg']-g['height_deg']-dh)<1e-7
    if i>=22:
     closed=ids[24-i];assert closed not in ws
     assert all(w['workspace'].get('parent')!=closed for w in ws.values()),'orphan caption'
    assert survivor in ws,'pre-existing app removed'
    stages.append(dict(step=i,phase=r['phase'],view=s['view'],rendered_generation=s['performance']['rendered_generation'],target_generation=r['target_generation'],windows=list(ws)))
   r=c.call('motion_stop');s=wait(r['target_generation']);assert s['view']==saved
   assert {w['window_id'] for w in s['windows']}==before,(before,s['windows'])
   checks.append('25 stages: mixed pitch/yaw/roll, caption drag position/size, overlap/focus, three close captions and restoration')
   # Disconnect cleanup, explicit stop from another client and starting again.
   temp=Client(path,'temporary-owner');r=temp.call('motion_start');wait(r['target_generation']);temp.close()
   deadline=time.monotonic()+30
   while other.call('get_state')['motion']['active']:
    assert time.monotonic()<deadline;time.sleep(.05)
   s=wait(other.call('sync_scene'));assert s['view']==saved
   r=c.call('motion_start');wait(r['target_generation']);c.call('input_button',dict(button='left',pressed=True));r=other.call('motion_stop');s=wait(r['target_generation']);assert s['view']==saved
   assert not s['pointer']['buttons']
   checks.append('disconnect and explicit stop release held input, restore pose and preserve original windows')
   # Run the user-facing CLI, including output fences and final cleanup.
   cli=subprocess.run([str(binary),'motion','1','0'],env=env,capture_output=True,text=True,timeout=180)
   (out/'cli.log').write_text(cli.stdout+cli.stderr)
   assert cli.returncode==0,cli.stdout+cli.stderr
   assert cli.stdout.count('BOAYO_MOTION_STEP ')==25 and 'BOAYO_MOTION_COMPLETE' in cli.stdout
   checks.append('native motion CLI completes all 25 output-fenced steps')
   master,slave=pty.openpty();tty_before=termios.tcgetattr(slave)
   keys=subprocess.Popen([str(binary),'keys','2'],env=env,stdin=slave,stdout=slave,stderr=slave)
   data=bytearray()
   def until(text):
    end=time.monotonic()+30
    while text not in data:
     assert time.monotonic()<end and keys.poll() is None,(text,data)
     if select.select([master],[],[],.05)[0]:data.extend(os.read(master,65536))
   until(b'BOAYO_KEYS_READY');assert b'Z/C=roll' in data
   for key,roll in [(b'z',9.),(b'c',11.)]:
    data.clear();os.write(master,key)
    until(b'"roll":'+str(roll).encode());assert other.call('view_state')['roll']==roll
   os.write(master,b'q');assert keys.wait(timeout=30)==0
   assert termios.tcgetattr(slave)==tty_before
   os.close(master);os.close(slave);checks.append('PTY Z/C roll input and terminal restoration')
   interrupted=subprocess.Popen([str(binary),'motion','100','1000'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   first=interrupted.stdout.readline();assert 'BOAYO_MOTION_BEGIN' in first,first
   interrupted.send_signal(signal.SIGINT);tail,errors=interrupted.communicate(timeout=90)
   assert interrupted.returncode==130 and 'BOAYO_MOTION_STOPPED restored' in tail,(tail,errors)
   s=wait(other.call('sync_scene'));assert not s['motion']['active'] and s['view']==saved
   assert {w['window_id'] for w in s['windows']}==before
   checks.append('Ctrl-C stops after in-flight completion, restores pose/scene and leaves existing app alive')
   c.call('shutdown');c.close();other.close();assert proc.wait(timeout=30)==0
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=30)
report=dict(result='PASS_MOCK_MOTION',hardware=False,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),checks=checks,stages=stages)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(result=report['result'],checks=checks)))
