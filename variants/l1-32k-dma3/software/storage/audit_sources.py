"""Record exact candidate source bytes and verify preserved scheduler/vendor IP."""
import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
base=root.parent/'sd4-dma'
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def files(r):
    return {p.relative_to(r).as_posix():sha(p) for folder in ['rtl','gen2','soc','software']
            for p in (r/folder).rglob('*') if p.is_file() and p.suffix in
            {'.sv','.v','.vh','.vhd','.xdc','.prj','.f','.tcl','.c','.h','.py','.sh','.dts'}}
a=files(root);b=files(base)
changed=sorted(k for k in a if k in b and a[k]!=b[k]);added=sorted(a.keys()-b.keys())
for folder in ['rtl/','gen2/','soc/vendor/','soc/board/vendor/']:
    assert {k:v for k,v in a.items() if k.startswith(folder)}=={k:v for k,v in b.items() if k.startswith(folder)},folder
assert a['soc/board/mig_a.prj']==b['soc/board/mig_a.prj']
(out/'source_manifest.json').write_text(json.dumps(a,indent=2)+'\n',encoding='utf-8')
(out/'change_audit.json').write_text(json.dumps({'changed':changed,'added':added,
 'scheduler_original_sources':'IDENTICAL','bosio_vendor_sources':'IDENTICAL',
 'hdmi_ip_internal':'IDENTICAL','mig_configuration':'IDENTICAL'},indent=2)+'\n',encoding='utf-8')
print('PASS preserved scheduler, BOSIO vendor, HDMI IP, MIG configuration; source files',len(a))
