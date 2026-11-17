from pathlib import Path
import time,re,json,datetime
r=Path(__file__).parent
rx=re.compile(r'BOAYO_INITTRACE n=(\d+) event=(\S+) wall_us=(-?\d+) cpu_us=(-?\d+) value=(-?\d+)')
seen=0;deadline=time.monotonic()+900
while time.monotonic()<deadline:
 items=[dict(n=int(n),event=e,wall_us=int(w),cpu_us=int(c),value=int(v)) for n,e,w,c,v in rx.findall((r/'uart.log').read_text(errors='replace'))]
 for item in items[seen:]:print(json.dumps(item),flush=True)
 seen=len(items)
 (r/'trace_progress.json').write_text(json.dumps(dict(observed_at=datetime.datetime.now().isoformat(),records=items),indent=2))
 events=[x['event'] for x in items]
 if 'surface.clear.end' in events:
  a=next(x for x in items if x['event']=='surface.clear.begin');b=next(x for x in items if x['event']=='surface.clear.end')
  assert any(x['event']=='rgb.dma.begin' and a['n']<x['n']<b['n'] for x in items)
  assert any(x['event']=='rgb.dma.end' and a['n']<x['n']<b['n'] for x in items)
  wall=(b['wall_us']-a['wall_us'])/1e6;cpu=(b['cpu_us']-a['cpu_us'])/1e6
  state=json.loads((r/'result.json').read_text());state.update(result='PASS_BOARD_RGB_DMA_AND_LAUNCHER_BACKGROUND',background_wall_seconds=wall,background_cpu_seconds=cpu,baseline_wall_seconds=311.613570,wall_reduction_percent=(1-wall/311.613570)*100,stage='launcher_drawing',launcher_verified=False,hdmi_verified=False)
  (r/'result.json').write_text(json.dumps(state,indent=2));print(json.dumps(state),flush=True);break
 time.sleep(1)
else:raise TimeoutError('No launcher background completion within observation window; board left running')
