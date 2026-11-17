import sys,json,subprocess,tempfile,ast,math
from pathlib import Path
from types import SimpleNamespace
import numpy as np
here=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(here/'reference'/p) for p in ('apps','wm','platform')]
from boayo_shell import BoayoLauncherShell
from boayo_app_window import BoayoAppFrame
from boayo_caption import button_image
from boayo_spatial_workspace import SpatialWorkspace
from boayo_theme import *
proc=subprocess.Popen([sys.argv[1],'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
count=0
with tempfile.TemporaryDirectory() as tmp:
 def compare(args,expected):
  global count
  args=dict(args,path=str(Path(tmp)/'pixels.bin'))
  proc.stdin.write(json.dumps(dict(op='test_ui',args=args))+'\n');proc.stdin.flush()
  result=json.loads(proc.stdout.readline());assert result['ok'],result
  actual=np.fromfile(args['path'],dtype=np.uint8).reshape(expected.shape)
  mismatch=np.any(actual!=expected,axis=-1)
  assert not mismatch.any(),(args,int(mismatch.sum()),np.max(np.abs(actual.astype(int)-expected.astype(int))))
  count+=1
 for name in ('move','resize_right','close'):
  for selected in (False,True):
   for height in (40,100):compare(dict(kind='caption',name=name,selected=selected,height=height),button_image(name,selected,height))
 apps=json.loads((here/'reference/apps/apps.json').read_text())['apps'];apps=[a for a in apps if a.get('enabled') and a.get('command')]
 for selected,hovered,offset,visible,active in [(-1,-1,0,True,False),(0,1,0,True,False),(2,2,1,True,False),(-1,-1,0,False,False),(-1,-1,0,True,True)]:
  shell=BoayoLauncherShell();shell.apps=apps;shell.selected_app=apps[selected] if selected>=0 else None;shell.hovered_app=apps[hovered] if hovered>=0 else None;shell.scroll_offset=offset;shell.visible=visible;shell.active_app=apps[0] if active else None
  compare(dict(kind='launcher',apps=apps,selected=selected,hovered=hovered,offset=offset,visible=visible,active=active),shell.render())
 for focused in (False,True):
  for external in (False,True):
   for hovered in (None,'move','resize_right','close'):
    frame=BoayoAppFrame(400,300,'Example');frame.focused=focused;frame.external_controls=external;frame.hovered=hovered
    compare(dict(kind='frame',focused=focused,external=external,hovered=hovered or ''),frame.render(lambda c,r:None))
 workspace=SpatialWorkspace(None)
 for selected in (False,True):
  for dim in (False,True):
   for closing in (False,True):compare(dict(kind='storage',title='SDK Pulse',selected=selected,dim=dim,closing=closing),workspace._card_image({'title':'SDK Pulse'},selected,dim,closing))
 for app,module in [('dashboard','boayo_example_app'),('telemetry','boayo_telemetry_app'),('sdk-pulse','boayo_pulse_app')]:
  # Execute the original application's actual draw callback AST, with frozen time/state.
  tree=ast.parse((here/'reference/apps'/f'{module}.py').read_text());draw=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='draw')
  for focused in (False,True):
   for step in (0,1,9):
    width,height=(640,440) if app=='dashboard' else (400,300)
    s=dict(width=width,height=height,focused=focused,step=step,clicks=4,apps=3,stored=1,yaw=0,pitch=0,width_deg=38.,height_deg=28.)
    env=dict(np=np,math=math,t=step,step=step,clicks=4,apps=[1,2,3],stored=1,output={},window=None,sdk=SimpleNamespace(window_state=lambda w:SimpleNamespace(focused=focused,width_deg=38.,height_deg=28.)),CHROME=CHROME,RAISED=RAISED,FOCUS=FOCUS,TEXT=TEXT,SUBTLE=SUBTLE)
    exec(compile(ast.Module(body=[draw],type_ignores=[]),'<original draw>','exec'),env)
    frame=BoayoAppFrame(width,height,{'dashboard':'Dashboard','telemetry':'Telemetry','sdk-pulse':'SDK Pulse'}[app]);frame.focused=focused;frame.external_controls=True
    compare(dict(kind='app',app=app,state=s),frame.render(env['draw']))
proc.stdin.close();assert proc.wait()==0
print(json.dumps(dict(result='PASS',pixel_exact_images=count,coverage='launcher, captions, chrome, storage, dashboard, telemetry, SDK Pulse')))
