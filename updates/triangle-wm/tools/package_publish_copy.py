"""Build a regular SD image with the experimental kernel and native benchmarks."""
import hashlib,importlib.util,json,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[3]
base=Path('/var/tmp/haebancheon-userland-validation');out=Path('/var/tmp/triangle-publish-copy')
images=out/'images';images.mkdir(exist_ok=True)
def run(*args):subprocess.run(list(map(str,args)),check=True)
spec=importlib.util.spec_from_file_location('userland_image',repo/'software/userland/make_image.py');image=importlib.util.module_from_spec(spec);spec.loader.exec_module(image)
board=repo/'variants/l1-16k-dma3-lru-ram'
run('python3',board/'soc/board/make_sd.py',images/'boot','--firmware',out/'firmware')
boot=images/'boot/ORYUKDO-Gen3-BOSIO-KFB-1GiB-BusyBox.img';dtb=images/'boot/gen3.dtb'
run('python3',board/'soc/board/verify_image.py',boot,out/'firmware',dtb)
mapping=image.check_bosio_output_map(dtb)
rootfs=out/'rootfs.ext4'
if not rootfs.exists():run('cp','--reflink=auto','--sparse=always',base/'buildroot/images/rootfs.ext4',rootfs)
for name in ['publish-bench','primitive-bench']:
 source=repo/'updates/triangle-wm/evidence/primitive'/name
 # Fresh rootfs only; an existing tool must not silently remain stale.
 run('debugfs','-w','-R',f'rm /usr/bin/{name}',rootfs)
 run('debugfs','-w','-R',f'write {source} /usr/bin/{name}',rootfs)
 run('debugfs','-w','-R',f'set_inode_field /usr/bin/{name} mode 0100755',rootfs)
header=board/'software/bosio/include/bosio_uapi.h'
run('debugfs','-w','-R','rm /usr/include/bosio_uapi.h',rootfs)
run('debugfs','-w','-R',f'write {header} /usr/include/bosio_uapi.h',rootfs)
run('debugfs','-w','-R','set_inode_field /usr/include/bosio_uapi.h mode 0100644',rootfs)
run('e2fsck','-fn',rootfs)
target=images/'HAEBANCHEON-publish-copy.img'
manifest=image.assemble(boot,rootfs,target)
audit=image.inspect_image(target);assert audit['segments']==manifest['segments']
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
manifest.update(image=target.name,bytes=target.stat().st_size,sha256=sha(target),kernel_sha256=sha(out/'firmware/Image'),rootfs_sha256=sha(rootfs),bosio_output_map=mapping,hardware_tested=False,requires='Unchanged L1-16K DMA3 LRU-RAM bitstream; reflash SD, boot experimental kernel',kernel_release='6.12.0-haebancheon-publishcopy',benchmarks=['/usr/bin/publish-bench','/usr/bin/primitive-bench'],command='publish-bench --copy',copy_api='BO_PUBLISH_COPY (CAP_SYS_RAWIO)',source_files={str(p.relative_to(repo)):sha(p) for p in [board/'common/bosio_engine.c'] if p.exists()})
manifest['source_files']={str(p.relative_to(repo)):sha(p) for p in [board/'software/bosio'/f for f in ['common/bosio_engine.c','common/bosio_engine.h','include/bosio_uapi.h','driver/bosio_kfb.c']]}
(images/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
