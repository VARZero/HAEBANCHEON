from board_io import *
import shutil,sys
deadline=time.monotonic()+900
while 'BASELINE_STAGED_RAM_ONLY' not in (root/'stage-baseline.log').read_text(errors='replace'):
 assert time.monotonic()<deadline,'baseline staging timeout'
 time.sleep(2)
def collect(label,cmd):
 raw=command(cmd,300);(root/(label+'.txt')).write_text(raw)
 rows=[json.loads(x) for x in re.findall(r'BENCH_SAMPLE (\{[^\r\n]+\})',raw)]
 assert rows and all(r['dma_errors']==0 and r['render_error'] is None and r['app_error'] is None for r in rows),rows
 (root/(label+'.json')).write_text(json.dumps(rows,indent=2));return rows
if '--resume-baseline' not in sys.argv:
 collect('v9-quiet','/tmp/d9-peek 1; sleep 30; /tmp/d9-peek 1')
 command('boayo-stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
command('BOAYO_INIT_TRACE=0 /tmp/boayo-v8-baseline start',120)
command('sleep 12; /tmp/boayo-v8-baseline app telemetry',180)
command('sleep 20',60)
collect('v8-telemetry-samples','/tmp/d9-peek 12')
collect('v8-quiet','/tmp/d9-peek 1; sleep 30; /tmp/d9-peek 1')
status('v8-final-state')
command('/tmp/boayo-v8-baseline stop && while test -S /tmp/bosio-wm.sock; do sleep 1; done',180)
command('BOAYO_INIT_TRACE=0 /usr/bin/boayo-native start',120)
command('sleep 12; /usr/bin/boayo-native app telemetry',180)
command('sleep 20',60)
collect('v9-restored-quiet','/tmp/d9-peek 1; sleep 30; /tmp/d9-peek 1')
status('v9-final-state')
shutil.copy2(root/'hdmi_live/latest.png',root/'telemetry-final.png')
print('COMPARE_COMPLETE_V9_RESTORED',flush=True)
