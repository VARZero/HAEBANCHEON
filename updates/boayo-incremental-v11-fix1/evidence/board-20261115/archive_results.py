from pathlib import Path
import json,hashlib,shutil
r=Path(__file__).parent
repo=Path('D:/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/ORYUKDO-3d5i_BOSIO_SoC')
variant=repo/'updates/boayo-incremental-v11'
dest=variant/'evidence/board-20261115'
dest.mkdir(exist_ok=True)
reports=json.loads((r/'comparison.json').read_text())
old=json.loads(Path('D:/temp/haebancheon-frame-v10/comparison.json').read_text())
legacy,fast,normal=reports
fixed=json.loads((r/'fix1-normal.json').read_text())
functional=json.loads((r/'functional-result.json').read_text())
assert functional['result']=='PARTIAL_DYNAMIC_GEOMETRY_DELAY'
summary={
 'scope':'Physical PZ-A7200T-KFB, unchanged L1-16K-DMA3 FPGA, v11 kernel/SD; v11 profile comparison, then fix1 app in RAM. Warmed Telemetry rendering, not boot duration or optical latency.',
 'normal_seconds_per_update':normal['seconds_per_update'],
 'normal_frames':normal['frames'],
 'normal_fps':1/normal['seconds_per_update'],
 'fix1_normal':fixed,
 'fix1_reduction_vs_previous_v10_percent':100*(1-fixed['seconds_per_update']/old['normal_quiet']['seconds_per_update']),
 'previous_v10_normal_seconds_per_update':old['normal_quiet']['seconds_per_update'],
 'reduction_vs_previous_v10_percent':100*(1-normal['seconds_per_update']/old['normal_quiet']['seconds_per_update']),
 'profiled_legacy_seconds_per_update':legacy['seconds_per_update'],
 'profiled_fast_seconds_per_update':fast['seconds_per_update'],
 'driver_time_unit':'Per-update milliseconds; original raw counter keys retain _us suffix in comparison.json although values were converted to ms.',
 'same_run_profile_comparison':{
   key:{'legacy_ms':legacy['software'][key]['wall_ms'],'fast_ms':fast['software'][key]['wall_ms']}
   for key in ['apps','workspace','state_publish','compose','blend','output_publish']},
 'functional_steps_completed':len(functional['steps']),
 'functional_result':functional['result'],
 'functional_implementation':'v11-fix1 in RAM; original v11 motion creation timed out and is preserved as a failure',
 'dma_errors':functional['dma']['errors'], 'dma_timeouts':functional['dma']['timeouts'],
 'limitations':['Short steady-state samples, previous v10 is an earlier boot; profiling itself costs time.',
   '2fps cap is not achieved; no RTL or new timing closure claim.',
   'Device acknowledgements and selected actual HDMI captures do not establish pixel-perfect motion.',
   'Pre-existing rough text/colored edges remain visible.',
   'The new fix1 SD image was audited and QEMU-booted, but has not been cold-booted on the physical board.',
   'The physical 25-stage motion test was not completed: step 11 spent several minutes generating new geometry masks on CPU. Ten view stages completed.']}
(r/'result-summary.json').write_text(json.dumps(summary,indent=2))
start=json.loads((r/'uart_start.json').read_text())['offset']
raw=Path('D:/temp/haebancheon-motion-v7/board-trial-20261114/uart.log').read_bytes()[start:]
(r/'uart-v11.log').write_bytes(raw)
for p in r.iterdir():
 if p.is_file() and p.suffix.lower() in ('.json','.txt','.log','.py','.tcl','.png'):
  shutil.copy2(p,dest/p.name)
fixdest=repo/'updates/boayo-incremental-v11-fix1/evidence/board-20261115'
shutil.copytree(dest,fixdest,dirs_exist_ok=True)
consistency=variant/'evidence/source-consistency.json'
d=json.loads(consistency.read_text());d['physical_new_driver_tested']=True
d['physical_evidence']='board-20261115/result-summary.json'
consistency.write_text(json.dumps(d,indent=2)+'\n')
print(json.dumps(summary,indent=2))
