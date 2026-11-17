from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
w=Path(sys.argv[1]);assets=w/'assets';manifest=json.loads((assets/'manifest.json').read_text());result=[]
def start(mode,enabled):
 return subprocess.Popen([str(w/mode/'boayo-native-host'),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_ASSET_DIR=str(assets),BOAYO_ASSETS=str(int(enabled)),BOAYO_INIT_TRACE='0'))
def call(p,op,args):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='boayo-desktop'))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert r['ok'],r;return r['result']
a,b=start('reference',False),start('host',True)
try:
 with tempfile.TemporaryDirectory() as tmp:
  tmp=Path(tmp)
  for entry in manifest['assets']:
   for p,name in [(a,'a'),(b,'b')]:call(p,'test_ui',dict(entry['args'],path=str(tmp/name)))
   x,y=(tmp/'a').read_bytes(),(tmp/'b').read_bytes();assert x==y;assert hashlib.sha256(y).hexdigest()==entry['pixels_sha256']
  for app in ['dashboard','telemetry','sdk-pulse']:
   call(b,'test_desktop',dict(op='launch',kind=app,azimuth=0,elevation=0))
  state=call(b,'get_state',{});bodies=[x for x in state['windows'] if x.get('workspace',{}).get('role')=='app'];buttons=[x for x in state['windows'] if x.get('workspace',{}).get('role')=='caption']
  assert len(bodies)==3 and all(x['shape']==[[48,4,x['surface_width']-96,x['surface_height']-12,0]] for x in bodies)
  assert buttons and all(x['shape'][0][4]==8 for x in buttons)
finally:
 for p in [a,b]:
  p.stdin.close();assert p.wait(timeout=30)==0,p.stderr.read()
old=Path('/var/tmp/haebancheon-motion-v7/release/data-root/boayo-assets');captions=[x for x in manifest['assets'] if x['key'][0]=='caption']
assert len(captions)==18
for c in captions:assert (old/c['file']).read_bytes()==(assets/c['file']).read_bytes()
report=dict(result='PASS_RECT_BODY_AND_UNCHANGED_CONTROLS',assets=689,button_assets_identical=18,app_shapes_rectangular=3,all_asset_pixels_match_reference=True,physical=False)
(w/'rect-assets.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
