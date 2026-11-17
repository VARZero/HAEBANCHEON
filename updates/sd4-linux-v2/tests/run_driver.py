"""Compile actual driver request functions with a modeled kernel/MMIO boundary."""
from pathlib import Path
import subprocess,tempfile,hashlib,json
here=Path(__file__).resolve().parent
driver=here.parent/'oryukdo_sd.c'
source=driver.read_text()
# No substitute transfer implementation: only remove Linux includes and the
# platform registration tail. All request, completion and IRQ logic is compiled.
body=source.split('static void or_sd_request(')[0]
body='\n'.join(line for line in body.splitlines() if not line.startswith('#include'))
with tempfile.TemporaryDirectory(prefix='oryukdo-sd-driver-') as out:
 p=Path(out)/'test.c'
 p.write_text((here/'driver_shim.h').read_text()+'\n'+body+'\n'+(here/'driver_cases.c').read_text())
 exe=Path(out)/'test'
 subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
  '-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('driver_sha256='+hashlib.sha256(driver.read_bytes()).hexdigest())
