import json,math,sys
from pathlib import Path
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def components():return [{'id':1,'type':'text','x':16,'y':12,'text':'TRIANGLE WM','scale':2,'color':255},{'id':2,'type':'text','x':16,'y':43,'text':'BOSIO','scale':2,'color':255},{'id':3,'x':16,'y':69,'w':180,'h':14,'color':228},{'id':4,'x':16,'y':92,'w':110,'h':14,'color':38},{'id':5,'type':'text','x':16,'y':120,'text':'FRAME 00','scale':2,'color':255}]
for stage in range(1,7):
 lines=['#stage '+str(stage)];plan=[]
 def add(op,label=None,**kw):
  q={'op':op,**kw};lines.append(json.dumps(q,separators=(',',':')));plan.append({'request':q,'label':label})
 def pause(name):lines.extend(['#checkpoint '+name,'#wait 1000'])
 def base():add('clear');add('create',id=1,color=84,width=24 if stage>=4 else 20,height=16 if stage>=4 else 14)
 def content(label=None):add('content',label,id=1,width=240,height=144,components=components())
 if stage==1:
  add('clear');add('create','1단계 / 최초 생성 (1회)',id=1,color=84,width=20,height=14);pause('solid')
  for n in range(22):add('visible',id=1,visible=False);add('visible','1단계 / 창 표시' if n else None,id=1,visible=True)
 elif stage==2:
  base()
  for n in range(22):add('move','2단계 / 창 이동' if n else None,id=1,yaw=-8+16*(n%11)/10,pitch=2*math.sin(n*.5))
  add('move',id=1,yaw=0,pitch=0)
 elif stage==3:
  base();add('create',id=2,color=164,yaw=-8,pitch=4,width=17,height=12);add('create',id=3,color=109,yaw=8,pitch=-4,width=17,height=12);pause('three-windows')
  for n in range(22):add('move','3단계 / 여러 창 이동' if n else None,id=2+n%2,yaw=-10+20*(n%11)/10,pitch=4 if n%2 else -4)
  add('raise','3단계 / 순서·닫기 (2회)',id=1);pause('raised');add('close','3단계 / 순서·닫기 (2회)',id=1);pause('restored')
 elif stage==4:
  base()
  for _ in range(3):content('4단계 / 정적 콘텐츠 배치 (3회)')
  pause('static-content')
 elif stage==5:
  base();content()
  for n in range(22):add('component','5단계 / 텍스트 부분 갱신' if n else None,id=1,component={'id':5,'type':'text','x':16,'y':120,'text':'FRAME '+str(n).zfill(2),'scale':2,'color':255})
  for n in range(22):add('component','5단계 / 막대 부분 갱신' if n else None,id=1,component={'id':4,'x':16,'y':92,'w':30+10*(n%15),'h':14,'color':38})
  add('component',id=1,component={'id':6,'x':55,'y':80,'w':65,'h':31,'color':255});pause('overlaid-component');add('remove_component',id=1,component_id=6);pause('erased-and-restored')
 elif stage==6:
  for count in [1,4,8,16,32]:
   add('clear')
   for i in range(count):add('create',id=i+1,color=[84,164,109,38][i%4],yaw=((i%8)-3.5)*5,pitch=((i//8)-1.5)*4,width=12,height=9)
   for n in range(12):add('move','6단계 / '+str(count)+'개 창 중 1개 이동' if n else None,id=count,yaw=-8+n*1.5,pitch=0)
   pause(str(count)+'-windows')
  base();content()
  for width,height in [(8,8),(60,30),(200,90)]:
   for n in range(12):add('component','6단계 / '+str(width)+'x'+str(height)+' 콘텐츠 갱신' if n else None,id=1,component={'id':8,'x':20,'y':30,'w':width,'h':height,'color':228 if n%2 else 38})
  add('remove_component',id=1,component_id=8)
  for count in [1,4,8]:
   add('clear')
   for i in range(count):
    add('create',id=i+1,color=84,yaw=(i%4-1.5)*12 if count>1 else 0,pitch=(-7 if i<4 else 7) if count>4 else 0,width=8,height=6,padding=1)
    add('content',id=i+1,width=64,height=64,components=[{'id':1,'type':'text','x':4,'y':4,'text':'W'+str(i+1),'scale':1,'color':255},{'id':2,'x':8,'y':24,'w':40,'h':12,'color':255}])
   for n in range(25):add('component','6단계 / 콘텐츠 창 '+str(count)+'개 순환 갱신' if n else None,id=1+n%count,component={'id':2,'x':8,'y':24,'w':40,'h':12,'color':228 if (n//count)%2 else 38})
   pause(str(count)+'-content-windows')
  base();content()
  for n in range(22):add('pose','6단계 / 시점 입력부터 완료 응답' if n else None,yaw=8*math.sin(n*.25),pitch=3*math.sin(n*.5))
 add('status','before-pose');add('pose_sweep','pose',samples=61,amplitude=8);add('status','after-pose');pause('final')
 (out/('stage'+str(stage)+'.requests')).write_text('\n'.join(lines)+'\n',encoding='utf-8')
 (out/('stage'+str(stage)+'.plan.json')).write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
print('Generated six deterministic suites')
