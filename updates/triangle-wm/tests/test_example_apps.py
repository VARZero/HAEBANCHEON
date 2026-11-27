"""Run each actual native app against the mock WM; exercise real UI events."""
import json,os,subprocess,sys,tempfile
from example_test_support import Control,until,screen
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary'};p=subprocess.Popen([sys.argv[1],'--mock'],env=env,stdout=subprocess.PIPE,text=True);assert p.stdout.readline().startswith('TRIANGLE_BOAYO_READY');ctl=Control(path+'.control');app=None
 try:
  note=folder+'/note.txt';open(note,'w').close()
  for kind in ['editor','settings','dashboard']:
   ctl.do('input_visibility',dict(visible=True));before=ctl.call('get_state')['performance']['shell_publications']
   app=subprocess.Popen([sys.argv[2],kind,'--file',note],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
   ready=app.stdout.readline();assert ready.startswith('EXAMPLE_READY '),(ready,app.stderr.read() if app.poll() is not None else '')
   info=json.loads(ready[len('EXAMPLE_READY '):]);wid=info['window_id'];state=ctl.call('get_state');assert state['windows'][0]['phase']=='content';assert state['performance']['shell_publications']==before+1
   def click(x,y):
    xy=screen(x,y);ctl.do('input_button',dict(**xy,pressed=True));ctl.do('input_button',dict(**xy,pressed=False))
   if kind=='editor':
    ctl.call('input_text',dict(text='hello\nworld!'));ctl.call('input_key',dict(key='Save'))
    until(lambda:open(note).read()=='hello\nworld!')
    until(lambda:ctl.call('get_state')['performance']['last_content_batch']['converted_pixels']<360*240)
   elif kind=='settings':
    click(195,72);click(290,214)
    until(lambda:ctl.call('get_state')['windows'][0]['splash_index']==3)
    click(150,170);until(lambda:not ctl.call('input_status')['display_enabled'])
   else:click(280,26)
   ctl.do('destroy_window',dict(window_id=wid));assert app.wait(timeout=10)==0,app.stderr.read();app=None
   until(lambda:not ctl.call('get_state')['windows'])
  print('PASS_NATIVE_EXAMPLES: S-first/full-ready frames, real editor typing/save, partial damage, independent settings, pointer toggle, proc dashboard and close')
 finally:
  if app and app.poll() is None:app.terminate();app.wait(timeout=10)
  ctl.call('shutdown');ctl.close();assert p.wait(timeout=10)==0
