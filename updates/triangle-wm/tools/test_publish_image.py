"""Focused boot check of the delivered image; no BOSIO timing emulation."""
import argparse,importlib.util,json,struct,subprocess,tempfile
from pathlib import Path
repo=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('smoke',repo/'software/userland/tests/qemu_smoke.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
ap=argparse.ArgumentParser();ap.add_argument('--fixed',action='store_true');ap.add_argument('--marker',action='store_true');ap.add_argument('--preview',choices=['tile','outline'],default='tile');ap.add_argument('--image',type=Path);ap.add_argument('--output',type=Path);args=ap.parse_args()
args.fixed=args.fixed or args.marker
image=args.image or Path('/var/tmp/triangle-fixed-image/HAEBANCHEON-fixed-wm-binary.img' if args.fixed else '/var/tmp/triangle-publish-copy/images/HAEBANCHEON-publish-copy.img')
out=args.output or Path('/var/tmp/triangle-fixed-image/focused-boot' if args.fixed else '/var/tmp/triangle-publish-copy/focused-boot');out.mkdir(parents=True,exist_ok=True)
layout=s.image.inspect_image(image)
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp);loaders=[]
 with image.open('rb') as f:
  f.seek(s.image.HEADER+512)
  for i,seg in enumerate(layout['segments']):
   p=root/str(i);p.write_bytes(f.read(seg['sectors']*512))
   if i!=2:loaders+=['-device',f'loader,file={p},addr={seg["address"]},force-raw=on']
 dts=root/'qemu.dts';dts.write_text(s.qemu_dts());dtb=root/'qemu.dtb'
 subprocess.run(['dtc','-I','dts','-O','dtb','-o',str(dtb),str(dts)],check=True)
 entry=root/'entry';entry.write_bytes(struct.pack('<4I',0x00000513,0x87f005b7,0x800002b7,0x00028067))
 loaders+=['-device',f'loader,file={dtb},addr=0x87f00000,force-raw=on','-device',f'loader,file={entry},addr=0x80200000,force-raw=on,cpu-num=0']
 cmd=['qemu-system-riscv32','-machine','virt','-cpu','rv32,c=false,f=false,d=false,v=false,zfa=false,zawrs=false','-m','1G','-smp','1','-bios','none','-display','none','-monitor','none','-serial','stdio','-no-reboot','-drive',f'file={image},if=none,format=raw,id=sd,snapshot=on','-device','virtio-blk-device,drive=sd,bus=virtio-mmio-bus.0']+loaders
 with (out/'uart.log').open('wb') as log:
  c=s.Console(cmd,log)
  try:
   c.until(rb'haebancheon# ',timeout=120)
   c.run("test $(uname -r) = 6.12.0-haebancheon-publishcopy && test -x /usr/bin/publish-bench && test -x /usr/bin/primitive-bench && grep -q BO_PUBLISH_COPY /usr/include/bosio_uapi.h",timeout=30)
   if args.fixed:
    c.run('sh -n /etc/init.d/S99triangle-fixed',timeout=30)
    if args.marker:
     c.run(f'TRIANGLE_PREVIEW={args.preview} triangle-fixed-start mock && cp /run/triangle-wm.pid /tmp/fixed.pid && cp /run/triangle-app.pid /tmp/demo.pid',timeout=120)
     c.run('readlink /proc/$(cat /tmp/fixed.pid)/exe | grep /run/triangle-wm-runtime/triangle-boayo-fixed',timeout=30)
    else:
     c.run('triangle-boayo-fixed --mock >/tmp/fixed.log 2>&1 & echo $! >/tmp/fixed.pid',timeout=30)
     c.run('triangle-app --rgb332 --demo >/tmp/demo.log 2>&1 & echo $! >/tmp/demo.pid',timeout=30)
    c.run('sleep 3',timeout=30)
    c.run('triangle-boayo-fixed --ctl \'{"op":"get_state"}\' | grep -F fixed-q24',timeout=120)
    c.run('triangle-input --stream-bench-async >/tmp/stream.log && grep STREAM_BENCH_DONE /tmp/stream.log',timeout=180)
    if args.marker:
     c.run('grep -E "fixed-wm-(single-tile-v3|tile-rim-v4|full-tile-v5|fast-motion-v6|fast-motion-v7|fast-motion-v8|fast-motion-v9|fast-resize-v10|fast-resize-v11|fast-resize-v12|solid-resize-v13)" /etc/triangle-wm-build.json',timeout=30)
     identity_text=c.run('cat /etc/triangle-wm-build.json',timeout=30).decode(errors='replace')
     build_identity=json.loads(identity_text[identity_text.index('{'):identity_text.rindex('}')+1])
     if build_identity.get('fast_motion'):c.run('triangle-input --fast-marker-bench >/tmp/fast.log && grep FAST_MARKER_BENCH_DONE /tmp/fast.log',timeout=180)
     if build_identity.get('fast_motion','').startswith('PTR1-one-way-drain'):
      c.run(r'''printf '@1001 500 350\n@1002 600 350\n{"op":"input_progress"}\nquit\n' | triangle-input --bridge-fast >/tmp/fast-drain.log && grep '"fast_accepted_sequence":1002' /tmp/fast-drain.log''',timeout=180)
     c.run('triangle-input --marker-bench 8 >/tmp/marker.log && grep MARKER_BENCH_DONE /tmp/marker.log',timeout=180)
     raw=c.run('cat /tmp/marker.log',timeout=60).decode(errors='replace')
     import re
     samples=[json.loads(x) for x in re.findall(r'(?:^|\n)MARKER_BENCH_SAMPLE (\{[^\r\n]+\})',raw)]
     assert [x['action'] for x in samples]==['move','resize']
     for row in samples:
      if args.preview=='tile':assert row['release']['result']['transform']['state']=='complete'
      base=row['before']['last_render']
      for move in row['moves']:
       if args.preview=='tile':
        assert move['preview']['mode'] in ['single-tile','tile-rim','solid-resize'] and move['preview']['frozen']
        for field in ['plan_builds_total','run_submissions_total']:assert move['render'][field]==base[field],(field,move)
        if move['preview']['mode']=='single-tile':
         assert move['render']['footprint_calls_total']==base['footprint_calls_total']
         if build_identity.get('move_marker_cells')==1024:
          assert move['preview']['colored_cells_per_tile']==1024 and move['preview']['payload_ops']==0 and move['preview']['payload_builds']==0
        else:assert len(move['preview'].get('filled_tiles',move['preview'].get('rim_tiles',[])))>4 and move['preview']['payload_ops']==0
       else:assert move['preview']['mode']=='outline'
      if args.preview=='outline':assert row['moves'][-1]['render']['footprint_calls_total']>base['footprint_calls_total']
     (out/'marker-samples.json').write_text(json.dumps(samples,indent=2)+'\n')
    c.run('kill $(cat /tmp/demo.pid); kill $(cat /tmp/fixed.pid)',timeout=30)
  finally:c.close()
report=dict(result='PASS_FOCUSED_QEMU_BOOT',image_sha256=s.image.sha(image),hardware=False,scope='Final image kernel release, userspace boot, benchmark executables and new UAPI header. No output hardware timing. Full unrelated mock-app suite was interrupted at shutdown wait.')
if args.fixed:report['scope']='Final image boot, actual RV32 fixed WM + RGB332 app + async input client exchanging BWM1 binary messages against mock BOSIO. Final cursor endpoints verified by native stream benchmark. No physical hardware/performance measurement.'
if args.marker:
 report['runtime_preview']=args.preview
 report['scope']+=(' Move marker and resize preview exercised on RV32; frozen motion has no descriptor/projection submissions; resize performs footprint geometry only; final transform completed.' if args.preview=='tile' else ' Legacy thin-outline reference mode exercised on the same image.')+' Emulator elapsed values are not hardware latency.'
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
