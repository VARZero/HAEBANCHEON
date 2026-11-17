from pathlib import Path
import subprocess,tempfile,hashlib,json
here=Path(__file__).resolve().parent
bosio=here.parent/'software/bosio'
driver=bosio/'driver/bosio_kfb.c'
body=driver.read_text().split('static int hw_read(')[0]
body='\n'.join(s for s in body.splitlines() if not s.startswith('#include'))
with tempfile.TemporaryDirectory(prefix='bosio-dma-test-') as temp:
 out=Path(temp)
 src=(here/'dma_shim.h').read_text()+'\n'+(bosio/'driver/bosio_dma_wait.h').read_text()+'\n'+body+'\n'+(here/'dma_cases.c').read_text()
 (out/'dma.c').write_text(src)
 rgb=(here/'dma_shim.h').read_text()+'\n'+(here/'rgb_shim.h').read_text()+'\n'+(bosio/'driver/bosio_dma_wait.h').read_text()+'\n'+body+'\n'+(bosio/'driver/bosio_rgb_dma.h').read_text()+'\n'+(here/'rgb_cases.c').read_text()
 (out/'rgb.c').write_text(rgb)
 direct=(here/'dma_shim.h').read_text()+'\n'+(here/'rgb_shim.h').read_text()+'\n'+(bosio/'driver/bosio_dma_wait.h').read_text()+'\n'+body+'\n'+(bosio/'driver/bosio_direct_dma.h').read_text()+'\n'+(here/'direct_cases.c').read_text()
 (out/'direct.c').write_text(direct)
 flags=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-I'+str(bosio/'common')]
 for name,sources in [('present',[here/'test_present.c',bosio/'common/bosio_engine.c']),('tiles',[here/'test_tiles.c',bosio/'common/bosio_engine.c']),('policy',[here/'test_memory_policy.c',bosio/'common/bosio_engine.c']),('dma',[out/'dma.c']),('rgb',[out/'rgb.c']),('direct',[out/'direct.c',bosio/'common/bosio_engine.c'])]:
  exe=out/name
  subprocess.run(flags+(['-Wno-unused-function'] if name in ('rgb','direct') else [])+[str(s) for s in sources]+['-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 exe=out/'dispatch'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(here/'test_rgb_dispatch.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print(json.dumps({'result':'PASS_HOST_KERNEL_BOUNDARY_MODEL','driver_sha256':hashlib.sha256(driver.read_bytes()).hexdigest(),'hardware':False}))
