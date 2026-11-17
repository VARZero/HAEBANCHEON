from pathlib import Path
import os,sys,json,subprocess,socket,tempfile,time
binary=sys.argv[1]
with tempfile.TemporaryDirectory() as td:
 path=td+'/wm.sock';env=dict(os.environ,BOAYO_SOCKET=path,BOAYO_PROFILE='1',BOAYO_MOTION_PROFILE='1')
 with open(td+'/server.log','w') as log:
  p=subprocess.Popen([binary,'serve','--mock'],env=env,stdout=log,stderr=log)
  try:
   end=time.monotonic()+60
   while not Path(path).exists():assert p.poll() is None and time.monotonic()<end;time.sleep(.05)
   s=socket.socket(socket.AF_UNIX);s.settimeout(60);s.connect(path);f=s.makefile('rwb',buffering=0);seq=0
   def call(op,args={}):
    global seq
    seq+=1;f.write((json.dumps(dict(id=seq,app='bounded-test',op=op,args=args))+'\n').encode());r=json.loads(f.readline());assert r['ok'],r;return r['result']
   call('set_frame_limit',dict(fps=60));initial=call('get_state')
   for n in ('0','26'):
    bad=subprocess.run([binary,'motion','1','0',n],env=env,capture_output=True,text=True,timeout=15);assert bad.returncode!=0
   run=subprocess.run([binary,'motion','1','0','11'],env=env,capture_output=True,text=True,timeout=120);assert run.returncode==0,run.stderr
   assert run.stdout.count('BOAYO_MOTION_STEP ')==11 and 'BOAYO_MOTION_PROFILE ' in run.stdout and 'BOAYO_MOTION_COMPLETE' in run.stdout
   state=call('get_state');assert not state['motion']['active'] and state['view']==initial['view'];assert {w['window_id'] for w in state['windows']}=={w['window_id'] for w in initial['windows']}
   profile=call('profile_progress');assert profile['projection']['geometry_builds']>0 and profile['projection']['geometry_descriptor_us_total']>0
   call('shutdown');f.close();s.close();assert p.wait(timeout=20)==0
  finally:
   if p.poll() is None:p.terminate();p.wait(timeout=20)
print('PASS_BOUNDED_MOTION_11_PROFILE_RESTORE_AND_RANGE_CHECKS')
