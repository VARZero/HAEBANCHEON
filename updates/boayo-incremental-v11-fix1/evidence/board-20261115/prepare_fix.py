from pathlib import Path
import subprocess
repo=Path('/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/ORYUKDO-3d5i_BOSIO_SoC')
r=Path('/var/tmp/haebancheon-incremental-v11-fix1')
subprocess.run(['/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-strip','-o',str(r/'boayo-fix1'),str(r/'rv32/boayo-native-rv32')],check=True)
s=(repo/'updates/boayo-view-v8/tools/make_delta.py').read_text()
s=s.replace("r=Path('/var/tmp/haebancheon-view-v8');old=Path(sys.argv[1] if len(sys.argv)>1 else '/var/tmp/haebancheon-motion-v7/release/root/usr/bin/boayo-native').read_bytes();new=(r/'boayo-v8').read_bytes()", "r=Path('/var/tmp/haebancheon-incremental-v11-fix1');old=Path('/var/tmp/haebancheon-incremental-v11/release/root/usr/bin/boayo-native').read_bytes();new=(r/'boayo-fix1').read_bytes()")
(r/'make_delta.py').write_text(s)
subprocess.run(['python3',str(r/'make_delta.py')],check=True)
Path('/mnt/d/temp/haebancheon-incremental-v11/board-trial/fix1.delta').write_bytes((r/'view.delta').read_bytes())
