from pathlib import Path
import json,re,subprocess
r=Path(__file__).parent
raw=Path('D:/temp/haebancheon-motion-v7/board-trial-20261114/uart.log').read_text(errors='replace')
motion=raw.split('oryukdo# /tmp/boayo-fix1 motion 1 0;',1)[1]
motion=motion.split('^Z',1)[0]
(r/'functional-partial.txt').write_text(motion)
steps=[json.loads(x) for x in re.findall(r'BOAYO_MOTION_STEP (\{[^\r\n]+\})',motion)]
assert len(steps)==10 and all(x['output_complete'] for x in steps)
final=json.loads((r/'final-restored-state.json').read_text())
assert final['app_error'] is None and final['render_error'] is None
assert not final['output']['mock_backend']
dma=final['output']['memory_dma'];assert dma['errors']==dma['timeouts']==0
(r/'functional-result.json').write_text(json.dumps(dict(
 result='PARTIAL_DYNAMIC_GEOMETRY_DELAY',steps=steps,
 scope='Physical fix1 executable in RAM over v11 kernel. Ten view steps completed; first window move generated CPU masks for several minutes. Diagnostic pause then intentional cancellation, not a full 25-step pass.',
 reason='PC samples: Compositor::prepare_masks, shape_contains, JSON arithmetic extraction and software double division.',
 restored='Fresh normal Telemetry service after terminating the CPU-bound test',
 dma=dma),indent=2))
subprocess.run(['C:/Python313/python.exe',str(r/'archive_results.py')],check=True)
