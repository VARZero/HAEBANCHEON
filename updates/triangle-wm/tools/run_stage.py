"""Host orchestration. Physical visual review follows each completed suite."""
import argparse,json,os,subprocess,sys,time
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('stage',type=int,choices=range(1,7));ap.add_argument('--output',required=True);a=ap.parse_args();out=Path(a.output).resolve();here=Path(__file__).resolve().parent;os.environ['PYTHONIOENCODING']='utf-8'
def run(name,*args):subprocess.run([sys.executable,str(here/name),*map(str,args)],check=True,stdout=subprocess.DEVNULL if name in ('uart.py','transfer.py') else None)
print('Running physical stage '+str(a.stage),flush=True)
target='/tmp/triangle-stage'+str(a.stage)+'.requests'
run('transfer.py',out/('stage'+str(a.stage)+'.requests'),target,'--receiver',out/'receiver','--output',out)
run('evidence.py','begin',a.stage,'--output',out,'--label',str(a.stage)+'단계 · 실제 보드 검증 중')
try:
 run('uart.py','/tmp/triangle-client '+target,'--timeout',900,'--output',out)
 run('collect_suite.py',out,a.stage)
 run('evidence.py','snapshot',a.stage,'--output',out)
 time.sleep(2) # allow the capture writer to close the recording after its status poll
 run('check_capture.py',out/('stage'+str(a.stage)+'.avi'))
except BaseException:
 state=json.loads((out/'status.json').read_text(encoding='utf-8'));state.update(stage=str(a.stage)+'단계 오류 · 로그 확인 필요',record=None);(out/'status.json').write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8');raise
