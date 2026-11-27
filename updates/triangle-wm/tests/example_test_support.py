import math,socket,time,os
import binary_test_wire as wire
class Control:
 def __init__(self,path):
  self.s=socket.socket(socket.AF_UNIX);self.s.settimeout(120 if os.environ.get('KEYBOARD_GUEST') else 10);self.s.connect(path);self.f=self.s.makefile('rb');self.seq=0
 def call(self,op,args=None):
  self.seq+=1;self.s.sendall(wire.encode(dict(id=self.seq,op=op,args=args or {})));r=wire.read(self.f);assert r['ok'],r;return r['result']
 def do(self,op,args=None):
  r=self.call(op,args)
  if 'generation' in r:self.call('wait_present',dict(generation=r['generation']))
  return r
 def close(self):self.f.close();self.s.close()
def until(fn):
 end=time.monotonic()+(90 if os.environ.get('KEYBOARD_GUEST') else 15)
 while True:
  result=fn()
  if result:return result
  assert time.monotonic()<end,'application did not reach expected state'
  time.sleep(.025)
def screen(x,y,yaw=0,pitch=0):
 u=(x/359-.5)*2*math.tan(math.radians(14));v=(.5-y/239)*2*math.tan(math.radians(10))
 a=math.radians(yaw);b=math.radians(pitch);xx=u*math.cos(a)-math.sin(a);zz=-u*math.sin(a)-math.cos(a)
 yy=v*math.cos(b)+zz*math.sin(b);zz=-v*math.sin(b)+zz*math.cos(b)
 return dict(x=round(639.5+xx/-zz/(2*math.tan(math.radians(24)))*1280),y=round(359.5-yy/-zz/(2*math.tan(math.radians(18)))*720))
