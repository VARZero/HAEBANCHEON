"""The actual demo performs diagnostics at startup only and exits on window close."""
import json,os,socket,subprocess,sys,tempfile,threading
import binary_test_wire as wire
with tempfile.TemporaryDirectory() as folder:
 path=folder+'/wm';surface=folder+'/surface';open(surface,'wb').truncate(240*144*2)
 listener=socket.socket(socket.AF_UNIX);listener.bind(path);listener.listen();ops=[];errors=[]
 def serve():
  try:
   s,_=listener.accept();f=s.makefile('rb');commits=0
   while True:
    try:q=wire.read(f)
    except (EOFError,ValueError):break
    op=q['op'];ops.append(op);r={};ok=True
    if op=='create_window':r=dict(window_id=1)
    elif op=='create_surface':r=dict(width=240,height=144,slot_bytes=240*144,path=surface)
    elif op=='sync_scene':r=1
    elif op=='commit_surface':
     commits+=1;r=dict(generation=commits+1)
     if commits==3:ok=False
    elif op=='get_state':r=dict(performance=dict(last_render={}))
    elif op!='wait_present':raise AssertionError(op)
    response=dict(id=q['id'],ok=ok)
    if ok:response['result']=r
    else:response['error']='window not found'
    s.sendall(wire.encode(response))
   f.close();s.close()
  except Exception as e:errors.append(repr(e))
 thread=threading.Thread(target=serve,daemon=True);thread.start()
 result=subprocess.run([sys.argv[1],'--rgb332','--demo'],env={**os.environ,'BOAYO_SOCKET':path,'TRIANGLE_WIRE':'binary'},capture_output=True,text=True,timeout=20)
 thread.join(5);listener.close();assert not errors,errors
 assert result.returncode==0,result
 assert ops.count('get_state')==1,ops
 assert ops.count('commit_surface')==3 and ops[-1]=='commit_surface',ops
 print('PASS_DEMO_QUERIES: startup diagnostic only, updates send damage and await publication, closed window exits normally')
