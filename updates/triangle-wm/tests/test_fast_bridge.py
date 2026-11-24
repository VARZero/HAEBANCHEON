"""Host mailbox receipt is distinct from board completion; flush precedes buttons."""
import ast,json,threading,time
from pathlib import Path
source=Path(__file__).resolve().parents[1]/'tools/pointer_bridge.py'
tree=ast.parse(source.read_text())
class UART:
 def __init__(self):self.writes=[]
 def write(self,data):self.writes.append(data)
 def flush(self):pass
uart=UART()
scope=dict(args=type('Args',(),dict(fast_input=True))(),uart=uart,online=True,latest={},
           motion_cv=threading.Condition(),pending_motion=None,motion_sequence=0,motion_sent=0,
           lock=threading.Lock(),time=time,json=json,log=type('Log',(),dict(write=lambda *a:None))(),
           line=lambda:'INPUT_REPLY {"ok":true,"result":{"pressed":false}}')
body=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in ['request','flush_motion']]
exec(compile(ast.Module(body=body,type_ignores=[]),str(source),'exec'),scope)
for i in range(100):
 r=scope['request'](dict(op='input_move',args=dict(x=i,y=300)))
 assert r['queued_to_uart'] and r['result']==dict(queued_sequence=i+1) and 'accept_ms' not in r
assert not uart.writes,'coordinate receipt waited for UART'
scope['request'](dict(op='input_button',args=dict(x=99,y=300,pressed=False)))
assert uart.writes[0]==b'@100 99 300\n' and json.loads(uart.writes[1])['op']=='input_button'
assert scope['pending_motion'] is None and scope['motion_sent']==100
for bad in [dict(x=1280,y=0),dict(x=1.5,y=0),dict(x=True,y=0)]:
 try:scope['request'](dict(op='input_move',args=bad));raise AssertionError('bad coordinate accepted')
 except ValueError:pass
print('PASS_FAST_BRIDGE: 100 inputs collapse to latest, truthful queued receipt, UART motion-before-button barrier, validation')
