from pathlib import Path
import json,re,datetime,shutil
r=Path(__file__).parent
b=Path('D:/temp/gen3bosio/boayo-tiles-v4/board_trial_20261113_005919')
state=json.loads((r/'result.json').read_text())
old=json.loads((b/'result.json').read_text())
def stamp(folder,pattern):
 data=(folder/'uart.log').read_bytes();m=re.search(pattern,data)
 if not m:return None
 for line in (folder/'uart_timeline.jsonl').read_text().splitlines():
  row=json.loads(line)
  if row['offset']+row['length']>=m.end():return row
old_kernel=stamp(b,rb'Linux version [^\n]*\n')
old_shell=stamp(b,rb'oryukdo# ')
old_kernel_seconds=(old_shell['monotonic_ns']-old_kernel['monotonic_ns'])/1e9
rows=[json.loads(x) for x in (r/'hdmi_usb/frames.jsonl').read_text().splitlines()]
first=next(x for x in rows if max(x['stddev'])>10)
idx=rows.index(first)
lastblank=rows[idx-1] if idx else None
shutil.copy2(r/'hdmi_usb'/first['path'],r/'hdmi_first_scene.png')
if lastblank:shutil.copy2(r/'hdmi_usb'/lastblank['path'],r/'hdmi_before_scene.png')
frame={x['event']:x for x in state['frame_trace']}
stage={}
for phase in ['plan.load','ready.prepare','texture.upload','projectors','output.versions','output.publish','output.upload']:
 if phase+'.begin' in frame and phase+'.end' in frame:stage[phase]=(frame[phase+'.end']['wall_us']-frame[phase+'.begin']['wall_us'])/1e6
state.update(hdmi_first_nonblank=first,hdmi_last_blank=lastblank,first_frame_stage_seconds=stage)
state['start_to_hdmi_first_nonblank_seconds']=(first['monotonic_ns']-state['start_command_monotonic_ns'])/1e9
state['hdmi_sample_period_seconds']=.5
state['baseline']={'path':str(b),'image':'tiles-v4 / no L1 / one DMA','kernel_banner_to_shell_seconds':old_kernel_seconds,'daemon_to_uart_present_seconds':old['start_to_present_seconds'],'frame_seconds':old['frame_seconds']}
state['comparison']={'kernel_banner_to_shell_speedup':old_kernel_seconds/state['kernel_banner_to_shell_seconds'],'daemon_to_uart_present_speedup':old['start_to_present_seconds']/state['start_to_present_seconds'],'daemon_to_uart_present_reduction_percent':100*(1-state['start_to_present_seconds']/old['start_to_present_seconds']),'scope':'Combined L1/DMA3 hardware and tiles-v5 software change. Cache-only effect not isolated.'}
(r/'result.json').write_text(json.dumps(state,indent=2)+'\n')
print(json.dumps({k:state[k] for k in ['program_to_shell_seconds','kernel_banner_to_shell_seconds','start_to_present_seconds','start_to_hdmi_first_nonblank_seconds','first_frame_stage_seconds','baseline','comparison','hdmi_first_nonblank']},indent=2))
