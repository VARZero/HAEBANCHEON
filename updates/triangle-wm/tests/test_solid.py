"""Host regression for whole-tile windows before physical stage verification."""
import json,subprocess,sys
commands=[{'op':'create','id':1,'color':84},{'op':'status'},
 {'op':'move','id':1,'yaw':0,'pitch':0},
 {'op':'visible','id':1,'visible':False},{'op':'visible','id':1,'visible':True},
 {'op':'create','id':2,'color':164,'yaw':4,'pitch':2},
 {'op':'raise','id':1},{'op':'close','id':1},{'op':'close','id':2},
 {'op':'status'},{'op':'create','id':3,'width':0}]
for y in [-179,-60,-12,0,12,60,179]:commands.extend([{'op':'create','id':4,'yaw':y,'pitch':0},{'op':'close','id':4}])
p=subprocess.run([sys.argv[1],'--mock'],input=''.join(json.dumps(c)+'\n' for c in commands),text=True,capture_output=True,check=True)
r=[json.loads(s) for s in p.stdout.splitlines()]
assert len(r)==len(commands)
assert r[0]['owned_tiles']>0 and r[0]['published']
assert r[1]['fill_bytes']==6*2*1024
assert r[2]['changed_tiles']==0 and not r[2]['published']
assert r[3]['owned_tiles']==0 and r[4]['owned_tiles']==r[0]['owned_tiles']
assert r[6]['changed_tiles']>0
assert r[8]['owned_tiles']==0 and r[9]['windows']==0
assert 'error' in r[10]
for index in range(11,len(r),2):assert r[index]['owned_tiles']>0 and r[index+1]['owned_tiles']==0
print('PASS_SOLID_WINDOW_REGRESSION create hide show no-op overlap raise close longitude-seam invalid-size')
