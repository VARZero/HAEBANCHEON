"""Collect post-route evidence, rejecting incomplete/timing-failing candidates."""
import hashlib,json,re,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
route=Path(sys.argv[1]);synth=Path(sys.argv[2]);out=Path(sys.argv[3])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(n):return (route/n).read_text(errors='replace')
timing=read('timing_routed.rpt'); lines=timing.splitlines()
header=next(i for i,l in enumerate(lines) if l.strip().startswith('WNS(ns)'))
row=lines[header+2].split()
t={k:float(row[i]) for k,i in {'wns':0,'tns':1,'whs':4,'ths':5,'wpws':8,'tpws':9}.items()}
assert t['wns']>=0 and t['whs']>=0 and t['wpws']>=0,t
assert t['tns']==t['ths']==t['tpws']==0,t
util=read('utilization.rpt')
def resource(label):
    m=re.search(r'^\|\s*'+re.escape(label)+r'\s*\|\s*([0-9.]+)',util,re.M)
    assert m,label
    return float(m.group(1))
r={k:resource(v) for k,v in {'lut':'Slice LUTs','ff':'Slice Registers',
    'bram_tiles':'Block RAM Tile','dsp':'DSPs','occupied_slices':'Slice'}.items()}
drc=read('drc.rpt');cdc=read('cdc.rpt')
assert not re.search(r'^CDC-\d+\s+(Warning|Critical)\s+[1-9]',cdc,re.M)
assert not re.search(r'^\|[^\n|]+\|\s*(Error|Critical Warning)\s*\|',drc,re.M)
warnings={m[0]:int(m[1]) for m in re.findall(r'^\|\s*([A-Z0-9-]+)\s*\|\s*Warning\s*\|[^|]+\|\s*(\d+)\s*\|',drc,re.M)}
assert warnings=={'BUFC-1':4,'REQP-1709':1},warnings
assert 'VIOLATED' not in read('bus_skew.rpt')
clocks=read('clocks.rpt')
periods={name:float(re.search(r'^'+name+r'\s+([0-9.]+)',clocks,re.M)[1])
         for name in ['cpu_raw','clk_pll_i','pixel_raw','serial_raw','sd_native_clk']}
assert periods['cpu_raw']==20 and periods['clk_pll_i']==10 and periods['sd_native_clk']==40,periods
manifest=json.loads((synth/'source_manifest.json').read_text())
for rel,value in manifest.items():
    # Exact synthesis inputs, excluding software helpers that can be added later.
    if Path(rel).suffix in {'.sv','.v','.vhd','.xdc','.prj','.f'}:
        assert sha(root/rel)==value,rel
bit=route/'ORYUKDO-Gen3-BOSIO-KFB.bit'
assert bit.is_file() and bit.stat().st_size>1000000
slacks={name:float(re.search(r'Slack\s+\(MET\)\s*:\s*([-0-9.]+)ns',
          read('timing_'+name+'.rpt'))[1]) for name in ['cpu_raw','clk_pll_i','pixel_raw']}
result={'result':'PASS_IMPLEMENTATION_PHYSICAL_BOARD_UNTESTED','part':'xc7a200tfbg484-2',
 'resources':r,'timing_ns':t,'period_ns':periods,'internal_setup_slack_ns':slacks,
 'cdc_warning_critical':0,'known_mig_drc_warnings':warnings,
 'bitstream_sha256':sha(bit),'checkpoint_sha256':sha(route/'routed.dcp'),
 'implementation_source_manifest_sha256':sha(synth/'source_manifest.json'),
 'synthesis_directory':str(synth),'implementation_directory':str(route),
 'source_match':True,'physical_board_tested':False,
 'implementation_scripts_sha256':{p.name:sha(p) for p in (route/'scripts').glob('*.tcl')},
 'raw_tool_critical_warnings':[l for l in read('vivado.log').splitlines() if l.startswith('CRITICAL WARNING:')]}
out.mkdir(parents=True,exist_ok=True)
(out/'board_manifest.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
