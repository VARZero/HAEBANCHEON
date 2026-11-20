"""Compare incremental bank contents against independently rebuilt full scenes."""
import json,subprocess,sys
class WM:
 def __init__(self):self.p=subprocess.Popen([sys.argv[1],'--mock'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 def call(self,q):
  self.p.stdin.write(json.dumps(q)+'\n');self.p.stdin.flush();r=json.loads(self.p.stdout.readline());assert 'error' not in r,r;return r
 def close(self):self.p.stdin.close();assert self.p.wait()==0
main=WM();reference=WM()
base={'op':'create','id':1,'color':84}
components=[{'id':1,'x':15,'y':18,'w':100,'h':40,'color':255},{'id':2,'x':70,'y':35,'w':100,'h':50,'color':228},{'id':3,'type':'text','x':20,'y':105,'text':'WM 012','scale':2,'color':255}]
def content():return {'op':'content','id':1,'width':240,'height':144,'components':components}
main.call(base);attached=main.call(content());assert attached['projected_tiles']>0 and all(attached['jobs_per_core'])
def compare(extra=None):
 reference.call({'op':'clear'});reference.call(base);reference.call(content())
 if extra:reference.call(extra)
 a=main.call({'op':'snapshot'})['tile_hashes'];b=reference.call({'op':'snapshot'})['tile_hashes'];assert a==b,[i for i,(x,y) in enumerate(zip(a,b)) if x!=y][:20]
compare()
for n in range(8):
 components[0]={**components[0],'x':15+n*2,'w':80-n*3,'color':255 if n%2 else 38}
 r=main.call({'op':'component','id':1,'component':components[0]});assert 0<r['projected_tiles']<attached['projected_tiles'];compare()
main.call({'op':'remove_component','id':1,'component_id':2});components.pop(1);compare()
# A hidden content mutation must appear when the covering whole-tile window closes.
cover={'op':'create','id':2,'yaw':4,'color':164,'width':12,'height':10}
main.call(cover);components[0]={**components[0],'color':109};main.call({'op':'component','id':1,'component':components[0]});compare(cover)
main.call({'op':'close','id':2});compare()
for yaw,pitch in [(5,2),(-8,-4),(0,0)]:
 base={**base,'yaw':yaw,'pitch':pitch};main.call({'op':'move','id':1,'yaw':yaw,'pitch':pitch});compare()
main.call({'op':'visible','id':1,'visible':False});main.call({'op':'visible','id':1,'visible':True});compare()
unchanged=main.call({'op':'component','id':1,'component':components[0]});assert not unchanged['published'] and unchanged['projected_tiles']==0
# Exercise a second resident source, so returning to the first cannot reuse the wrong texture.
main.call({'op':'create','id':2,'color':164,'yaw':-5,'width':10,'height':10})
main.call({'op':'content','id':2,'width':64,'height':64,'components':[{'id':1,'x':3,'y':3,'w':45,'h':45,'color':255}]})
components[0]={**components[0],'color':228};main.call({'op':'component','id':1,'component':components[0]})
main.call({'op':'close','id':2});compare()
before=main.call({'op':'status'})['commits'];main.call({'op':'pose','yaw':5,'pitch':2});assert main.call({'op':'status'})['commits']==before
main.close();reference.close();print('PASS_CONTENT_INCREMENTAL_EQUALS_FULL overlap erase damage bank-copy hidden-update move pose-only')
