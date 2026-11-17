from board_validate import *
state={'result':'RUNNING','stage':'boayo_start','physical_board':True,'hdmi_verified':False,'image':'HAEBANCHEON-L1-16K-DMA3-v1'}
def save():
 (r/'result.json').write_text(json.dumps(state,indent=2)+'\n')
def stamp(pattern):
 data=log.read_bytes();m=re.search(pattern,data)
 if not m:return None
 for line in (r/'uart_timeline.jsonl').read_text().splitlines():
  row=json.loads(line)
  if row['offset']+row['length']>=m.end():return row
def traces():
 text=log.read_text(errors='replace')
 for prefix,name in [('BOAYO_INITTRACE','init_trace'),('BOAYO_FRAME_TRACE','frame_trace')]:
  rows=[dict(n=int(n),event=e,wall_us=int(w),cpu_us=int(c),value=int(v)) for n,e,w,c,v in re.findall(prefix+r' n=(\d+) event=(\S+) wall_us=(-?\d+) cpu_us=(-?\d+) value=(-?\d+)\r*\n',text)]
  state[name]=list({x['n']:x for x in rows}.values())
  if rows:state['latest_'+name]=rows[-1]
 return text
state['shell_received']=stamp(rb'oryukdo# ')
state['kernel_received']=stamp(rb'Linux version [^\n]*\n')
state['first_uart']=json.loads((r/'uart_timeline.jsonl').read_text().splitlines()[0])
program=(r/'vivado.log').read_text(errors='replace')
state['program_end_epoch_ms']=int(re.search(r'^PROGRAM_END_MS=(\d+)',program,re.M)[1])
state['program_to_shell_seconds']=datetime.datetime.fromisoformat(state['shell_received']['utc']).timestamp()-state['program_end_epoch_ms']/1000
state['kernel_banner_to_shell_seconds']=(state['shell_received']['monotonic_ns']-state['kernel_received']['monotonic_ns'])/1e9
state['start_command_monotonic_ns']=time.monotonic_ns()
state['start_command_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save()
try:
 command('boayo-start',300)
 state['stage']='first_frame';save()
 end=time.monotonic()+900
 while time.monotonic()<end:
  text=traces();save()
  if re.search(r'event=output.present.done[^\n]*\n',text):break
  if 'BOSIO_RENDER_ERROR' in text or 'unhandled signal' in text:raise RuntimeError('render error observed')
  time.sleep(1)
 else:raise TimeoutError('first scene not presented within 900s')
 state['present_received']=stamp(rb'BOAYO_FRAME_TRACE[^\n]*event=output.present.done[^\n]*\n')
 state['start_to_present_seconds']=(state['present_received']['monotonic_ns']-state['start_command_monotonic_ns'])/1e9
 state['stage']='status';save()
 data=command('boayo-status; cat /tmp/boayo-wm.log',300).decode(errors='replace')
 obj,_=json.JSONDecoder().raw_decode(data[data.index('{'):]);state['status']=obj
 assert obj['render_error'] is None and obj.get('app_error') is None,obj
 assert not obj['output']['mock_backend'] and obj['performance']['render_count']>=1,obj
 state['result']='PASS_BOARD_FIRST_FRAME_UART';state['stage']='complete';traces();save()
except BaseException as exc:
 traces();state.update(result='REQUIRES_REVIEW',error=repr(exc));save();raise
print(json.dumps({k:v for k,v in state.items() if k not in ('status','init_trace','frame_trace')}),flush=True)
