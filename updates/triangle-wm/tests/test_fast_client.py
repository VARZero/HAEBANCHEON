"""Actual native UART bridge drains buffered motion and preserves release order."""
import json,os,socket,subprocess,sys,tempfile
import binary_test_wire as wire
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary'}
 server=subprocess.Popen([sys.argv[1],'--mock'],env=env,stdout=subprocess.PIPE,text=True)
 try:
  assert server.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
  s=socket.socket(socket.AF_UNIX);s.settimeout(30);s.connect(path+'.control');f=s.makefile('rb');seq=0
  app=socket.socket(socket.AF_UNIX);app.settimeout(30);app.connect(path);af=app.makefile('rb')
  def call(op,args=None):
   global seq
   seq+=1;conn,reader=(app,af) if op=='create_window' else (s,f)
   conn.sendall(wire.encode(dict(id=seq,op=op,args=args or {})));r=wire.read(reader);assert r['ok'],r;return r['result']
  r=call('create_window',dict(width_deg=20,height_deg=14,surface_width=32,surface_height=32));wid=r['window_id'];call('wait_present',dict(generation=r['generation']))
  h=next(c for c in call('input_status')['controls'] if c['action']=='move');x,y=h['x'],h['y']
  commands=json.dumps(dict(op='input_button',args=dict(x=x,y=y,pressed=True)))+'\n'
  commands+=''.join(f'@{i} {x+min(i,60)} {y}\n' for i in range(1,201))
  commands+=json.dumps(dict(op='input_button',args=dict(x=x+60,y=y,pressed=False)))+'\n'
  commands+=json.dumps(dict(op='input_progress'))+'\nquit\n'
  result=subprocess.run([sys.argv[2],'--bridge-fast'],input=commands,env=env,text=True,capture_output=True,timeout=60);assert result.returncode==0,result
  assert result.stdout.splitlines()[0]=='INPUT_READY',result.stdout
  replies=[json.loads(line[12:]) for line in result.stdout.splitlines() if line.startswith('INPUT_REPLY ')]
  assert len(replies)==3 and all(r['ok'] for r in replies),replies
  assert replies[1]['result']['transform']['state']=='complete',replies
  progress=replies[2]['result'];assert progress['fast_accepted_sequence']==200 and progress['fast_visible_sequence']==200,progress
  assert progress['accepted_ticket']<=2,'buffered UART coordinates were forwarded individually'
  state=call('input_status');assert not state['pressed'] and state['x']==x+60 and not state['preview']['active'],state
  assert call('get_state')['windows'][0]['azimuth']!=0
  # The shared ordinary command handler retains the legacy async behavior.
  result=subprocess.run([sys.argv[2],'--bridge-async'],input=json.dumps(dict(op='input_move',args=dict(x=640,y=360)))+'\nquit\n',env=env,text=True,capture_output=True,timeout=30)
  assert result.returncode==0 and '"completion_pending":true' in result.stdout,result
  f.close();s.close();af.close();app.close();print('PASS_FAST_CLIENT: 200 buffered coordinates drain to latest, ordered real release, distinct progress, no motion replies, legacy async retained')
 finally:
  server.terminate();server.wait(timeout=15);assert server.returncode==0,server.returncode
