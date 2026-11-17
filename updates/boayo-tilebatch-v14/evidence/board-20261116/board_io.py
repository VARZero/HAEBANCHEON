from pathlib import Path
import datetime,json,re,time,uuid
root=Path(__file__).parent
queue=root
log=queue/'uart.log'
def wait(pattern,start,timeout=300):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  with log.open('rb') as f:f.seek(start);data=f.read()
  m=re.search(pattern,data)
  if m:return data[:m.end()]
  time.sleep(.25)
 raise TimeoutError(pattern)
def command(cmd,timeout=300):
 token='V14_'+uuid.uuid4().hex[:8];start=log.stat().st_size;begin=time.monotonic()
 (queue/('send_'+str(time.time_ns())+'.txt')).write_bytes((cmd+'; echo '+token+'_RC:$?\r').encode())
 raw=wait(rb'\n'+token.encode()+rb'_RC:([0-9]+)\r*\n',start,timeout)
 rc=int(re.search(rb'\n'+token.encode()+rb'_RC:(\d+)',raw)[1])
 report=dict(command=cmd,seconds=round(time.monotonic()-begin,3),rc=rc,uart=raw.decode(errors='replace'),observed_at=datetime.datetime.now().isoformat(),log_start=start)
 (root/(token+'.json')).write_text(json.dumps(report,indent=2))
 print(json.dumps({k:v for k,v in report.items() if k!='uart'}),flush=True)
 assert rc==0,report
 return report['uart']
def status(label):
 raw=command('boayo-status');state=json.JSONDecoder().raw_decode(raw[raw.index('{'):])[0]
 (root/(label+'.json')).write_text(json.dumps(state,indent=2))
 assert state['render_error'] is None and state['app_error'] is None,state
 assert state['output']['mock_backend'] is False
 return state
if __name__=='__main__':
 import sys
 command(sys.argv[1],int(sys.argv[2]) if len(sys.argv)>2 else 300)
