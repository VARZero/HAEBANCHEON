from pathlib import Path
import subprocess,sys,json,hashlib,time,os
here=Path(__file__).resolve().parent
binary=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
results=[]
for test in ('test_core.py','test_ui.py','test_apps.py','test_compositor.py','test_fallback.py','test_runtime.py','test_responsive.py'):
 started=time.monotonic();p=subprocess.run([sys.executable,str(here/test),str(binary)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (out/(test+'.log')).write_text(p.stdout)
 item=dict(test=test,result='PASS' if p.returncode==0 else 'FAIL',seconds=round(time.monotonic()-started,2),exit_code=p.returncode)
 results.append(item);print(json.dumps(item),flush=True)
 if p.returncode:print(p.stdout[-6000:],flush=True)
for command in ('selftest','dual-selftest'):
 p=subprocess.run([str(binary),command],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (out/(command+'.log')).write_text(p.stdout);item=dict(test=command,result='PASS' if p.returncode==0 else 'FAIL',exit_code=p.returncode);results.append(item);print(json.dumps(item),flush=True)
 if p.returncode:print(p.stdout[-6000:],flush=True)
report=dict(result='PASS' if all(r['result']=='PASS' for r in results) else 'FAIL',binary=str(binary),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),tests=results,scope='host Python differential plus explicit mock backend',hardware=False)
(out/'suite.json').write_text(json.dumps(report,indent=2)+'\n')
sys.exit(0 if report['result']=='PASS' else 1)
