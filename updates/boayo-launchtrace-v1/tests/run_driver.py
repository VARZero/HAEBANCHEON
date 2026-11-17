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
 flags=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-I'+str(bosio/'common')]
 for name,sources in [('policy',[here/'test_memory_policy.c',bosio/'common/bosio_engine.c']),('dma',[out/'dma.c'])]:
  exe=out/name
  subprocess.run(flags+[str(s) for s in sources]+['-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
print(json.dumps({'result':'PASS_HOST_KERNEL_BOUNDARY_MODEL','driver_sha256':hashlib.sha256(driver.read_bytes()).hexdigest(),'hardware':False}))
