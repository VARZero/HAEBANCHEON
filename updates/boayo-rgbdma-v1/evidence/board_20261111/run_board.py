from board_validate import *
state={'result':'RUNNING','stage':'boot','hardware':True,'hdmi_verified':False}
def save():
 (r/'result.json').write_text(json.dumps(state,indent=2)+'\n')
 print(json.dumps(state),flush=True)
save()
try:
 wait(rb'oryukdo# ',0,1200)
 data=command('uname -r; cat /etc/oryukdo-image',180)
 assert b'6.12.0-oryukdo-rgbdma1' in data and b'ORYUKDO-BoAYO-rgbdma-v1' in data,'Wrong SD image'
 state.update(stage='dma_check',kernel_verified=True);save()
 data=command('bosio-dma-check',480)
 assert b'PASS BOSIO_DMA' in data
 state.update(stage='rgb_check',basic_dma='PASS');save()
 data=command('bosio-rgb-check',2400)
 assert b'PASS RGB_DMA' in data
 state.update(rgb_dma='PASS',rgb_times=re.findall(r'RGB_DMA_DONE offset=(\d+) bytes=(\d+) elapsed_ms=(\d+)',data.decode(errors='replace')),stage='boayo_start');save()
 command('boayo-start',300)
 state.update(result='PASS_BOARD_RGB_DMA_BOAYO_STARTED',stage='desktop_initialization',launcher_verified=False);save()
except BaseException as exc:
 state.update(result='FAIL_REQUIRES_REVIEW',error=repr(exc));save();raise
