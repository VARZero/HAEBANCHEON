from pathlib import Path
import subprocess,json,os
w=Path('/var/tmp/haebancheon-geometry-v13');here=Path(__file__).resolve().parent
p=subprocess.Popen([str(w/'host/boayo-native-host'),'--rpc'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,env=dict(os.environ,BOAYO_PLANS='0',BOAYO_ASSETS='0'))
def call(op,args):
 p.stdin.write(json.dumps(dict(op=op,args=args,app='bench'))+'\n');p.stdin.flush();r=json.loads(p.stdout.readline());assert r['ok'];return r['result']
a=call('create_window',dict(title='bench',surface_width=640,surface_height=440,azimuth=-10,elevation=4,width_deg=38,height_deg=28,shape=[[48,4,544,428,14]],decorated=False));r=call('test_plan_export',dict(window_id=a['window_id'],path=str(w/'bench-plan.bin')))
p.stdin.close();assert p.wait()==0
import struct
body=(w/'bench-plan.bin').read_bytes();rows=[]
for i in range(r['count']):
 rec=struct.unpack_from('<21I',body,i*84)
 if rec[1]==2:rows.append(rec[3:])
assert len(rows)>=16
rows=[rows[i*(len(rows)-1)//15] for i in range(16)]
(here/'mask_benchmark_data.inc').write_text('static const Descriptor samples[] = {\n'+''.join('{{'+','.join(str(x)+'u' for x in row)+'}},\n' for row in rows)+'};\n')
print('GENERATED_16_BOUNDARY_TILES')
