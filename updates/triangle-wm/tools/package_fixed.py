"""Package a regular SD image. Does not touch a physical board/disk."""
import argparse,hashlib,importlib.util,json,subprocess,tempfile
from pathlib import Path
repo=Path(__file__).resolve().parents[3]
ap=argparse.ArgumentParser();ap.add_argument('--build',type=Path,required=True);ap.add_argument('--output',type=Path,default=Path('/var/tmp/triangle-rim-image'));ap.add_argument('--name',default='HAEBANCHEON-tile-rim-preview.img');args=ap.parse_args()
base=Path('/var/tmp/triangle-publish-copy');build=args.build;out=args.output;out.mkdir(parents=True,exist_ok=True)
features=json.loads((build/'features.json').read_text());assert features['preview'] in ['single-tile','central-move-tile-rim-resize','full-move-tile-rim-resize','full-move-solid-resize']
def run(*args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
def sha(p):return hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
spec=importlib.util.spec_from_file_location('image',repo/'software/userland/make_image.py');image=importlib.util.module_from_spec(spec);spec.loader.exec_module(image)
rootfs=out/'rootfs.ext4'
# Always start from the checked base; repeated debugfs directory creation can
# leave orphaned inodes even when it reports that a directory already exists.
run('cp','--reflink=auto','--sparse=always',base/'rootfs.ext4',rootfs)
def install(source,target,mode='0100755'):
 run('debugfs','-w','-R',f'rm {target}',rootfs)
 run('debugfs','-w','-R',f'write {source} {target}',rootfs)
 run('debugfs','-w','-R',f'set_inode_field {target} mode {mode}',rootfs)
 with tempfile.TemporaryDirectory() as folder:
  check=Path(folder)/'check';run('debugfs','-R',f'dump {target} {check}',rootfs)
  assert check.read_bytes()==Path(source).read_bytes(),target
files={}
for name in ['triangle-boayo-fixed','triangle-app','triangle-input']:
 install(build/name,'/usr/bin/'+name);files[name]=sha(build/name)
assert files==features['binaries'],'build binaries differ from audited features manifest'
script=repo/'updates/triangle-wm/tools/triangle-fixed-start'
install(script,'/usr/bin/triangle-fixed-start');install(script,'/etc/init.d/S99triangle-fixed')
native=repo/'updates/boayo-tilebatch-v14/software/boayo/native'
for directory in ['/usr/share/triangle-wm','/usr/share/triangle-wm/sdk','/usr/share/triangle-wm/sdk/src','/usr/share/triangle-wm/sdk/vendor']:
 stat=subprocess.run(['debugfs','-R',f'stat {directory}',str(rootfs)],capture_output=True,text=True,check=True)
 if 'Inode:' not in stat.stdout:run('debugfs','-w','-R',f'mkdir {directory}',rootfs)
for name in ['common.hpp','ipc.hpp','binary_wire.hpp']:
 install(native/'src'/name,'/usr/share/triangle-wm/sdk/src/'+name,'0100644')
install(native/'vendor/json.hpp','/usr/share/triangle-wm/sdk/vendor/json.hpp','0100644')
identity=dict(version='fixed-wm-single-tile-v3',geometry='integer Q24',angles='signed quarter degrees in 16-bit range; normalized yaw [-180,180)',wire='BWM1 little-endian length + CBOR integer payload; angle Q2, other fractions Q24',publication='BO_PUBLISH_COPY default; TRIANGLE_PUBLISH_COPY=0 selects checked baseline',preview='Frozen window, one central S-colored triangle patch during move; corner marker during resize. One final transform publication. TRIANGLE_PREVIEW=outline selects reference path.',execution='Startup stages executables in /run/triangle-wm-runtime before launch',feedback_target_ms=300,feedback_target_hardware_verified=False,hardware_tested=False,binaries=files)
if features['preview']=='central-move-tile-rim-resize':identity.update(version='fixed-wm-tile-rim-v4',preview='Move: 64 central cells of one tile. Resize: whole boundary triangles share immutable S-color payload; bank-local metadata differences only. Real scene and content remain frozen until one final transform publication.')
if features.get('move_marker_cells')==1024:identity.update(version='fixed-wm-full-tile-v5',move_marker_cells=1024,preview='Move: one complete 1024-cell S-colored triangle, sharing an immutable solid payload. Resize: unchanged whole boundary triangles sharing S-color payload. Frozen content, latest input and one final transform publication retained.')
if features.get('fast_motion'):identity.update(version='fixed-wm-fast-motion-v6',fast_motion=features['fast_motion'])
if features.get('fast_motion')=='PTR1-one-way-drain':identity['version']='fixed-wm-fast-motion-v7'
if features.get('fast_motion')=='PTR1-one-way-drain-bounded':identity['version']='fixed-wm-fast-motion-v8'
if features.get('demo_diagnostics')=='startup-only':identity.update(version='fixed-wm-fast-motion-v9',demo_diagnostics=features['demo_diagnostics'],runtime_priority=features['runtime_priority'])
if features.get('resize_preview')=='direct-cached':identity.update(version='fixed-wm-fast-resize-v10',resize_preview=features['resize_preview'])
if features.get('resize_preview','').startswith('direct-cached-q13'):identity.update(version='fixed-wm-fast-resize-v11',resize_preview=features['resize_preview'])
if features.get('resize_preview','').startswith('direct-cached-trig'):identity.update(version='fixed-wm-fast-resize-v12',resize_preview=features['resize_preview'])
if features.get('preview')=='full-move-solid-resize':identity.update(version='fixed-wm-solid-resize-v13',resize_preview=features['resize_preview'],window_colors=features['window_colors'],preview='Move: frozen real window and one complete shared S tile. Resize: hide captured window once, fill complete target footprint with shared S payload. Shrinking restores underlying scene, higher windows occlude. Release publishes B and latest content once.')
(out/'wm-build.json').write_text(json.dumps(identity,indent=2)+'\n');install(out/'wm-build.json','/etc/triangle-wm-build.json','0100644')
run('e2fsck','-fn',rootfs)
target=out/args.name
manifest=image.assemble(base/'images/boot/ORYUKDO-Gen3-BOSIO-KFB-1GiB-BusyBox.img',rootfs,target)
assert image.inspect_image(target)['segments']==manifest['segments']
manifest.update(identity,image=target.name,bytes=target.stat().st_size,sha256=sha(target),kernel_sha256=sha(base/'firmware/Image'),kernel_release='6.12.0-haebancheon-publishcopy',rootfs_sha256=sha(rootfs),notes='Driver improvement and kernel retained. Automatically stages WM/app/input in RAM and starts demo when /dev/bosio0 exists. Prepared offline; no physical board was contacted. Native/QEMU checks do not measure hardware latency.')
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
