"""Collect post-route evidence, rejecting incomplete/timing-failing candidates."""
import hashlib,json,re,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
route=Path(sys.argv[1]);synth=Path(sys.argv[2]);out=Path(sys.argv[3])
implementation_input=Path(sys.argv[4]) if len(sys.argv)>4 else synth/'optimized.dcp'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(n):return (route/n).read_text(errors='replace')
timing=read('timing_routed.rpt'); lines=timing.splitlines()
header=next(i for i,l in enumerate(lines) if l.strip().startswith('WNS(ns)'))
row=lines[header+2].split()
t={k:float(row[i]) for k,i in {'wns':0,'tns':1,'whs':4,'ths':5,'wpws':8,'tpws':9}.items()}
assert t['wns']>=0 and t['whs']>=0 and t['wpws']>=0,t
assert t['tns']==t['ths']==t['tpws']==0,t
for check in ['no_clock','unconstrained_internal_endpoints','multiple_clock','loops','latch_loops']:
    m=re.search(r'checking '+check+r' \((\d+)\)',timing)
    assert m and int(m[1])==0,(check,m[1] if m else 'missing')
route_status=read('route_status.rpt')
def nets(label):
    m=re.search(re.escape(label)+r'\.+\s*:\s*(\d+)',route_status)
    assert m,label
    return int(m[1])
assert nets('# of routable nets')==nets('# of fully routed nets')>0
assert nets('# of nets with routing errors')==0
log=read('vivado.log')
assert 'PASS KFB_BOSIO_BOARD_ROUTE' in log
assert 'PASS KFB_BOARD_ROUTED_AUDIT' in log
util=read('utilization.rpt')
def resource(label):
    m=re.search(r'^\|\s*'+re.escape(label)+r'\s*\|\s*([0-9.]+)',util,re.M)
    assert m,label
    return float(m.group(1))
r={k:resource(v) for k,v in {'lut':'Slice LUTs','ff':'Slice Registers',
    'bram_tiles':'Block RAM Tile','dsp':'DSPs','occupied_slices':'Slice'}.items()}
hierarchy=[]
for line in read('hierarchy.rpt').splitlines():
    cells=[c.strip() for c in line.split('|')[1:-1]]
    if len(cells)==10 and cells[2].isdigit() and not cells[0].startswith('('):
        hierarchy.append(cells)
modules={}
for name,instance,module in [
    ('mig','ddr','mig_7series_0'),('hdmi','hdmi_output','kfb_hdmi'),
    ('cache_pair','bridge','or_l1_cpu'),('l1i','instruction_cache','or_l1_cache'),
    ('l1d','data_cache','or_l1_cache'),('dma0','dma','or_storage_dma'),
    ('dma1','extra_dma[0].engine','or_storage_dma'),('dma2','extra_dma[1].engine','or_storage_dma'),
    ('projection0','projection0','bosio_projector'),('projection1','projection1','bosio_projector'),
    ('output','output0','bosio_output_top')]:
    matches=[c for c in hierarchy if c[0]==instance and c[1].startswith(module)]
    assert len(matches)==1,(name,matches)
    c=matches[0]
    modules[name]={'lut':int(c[2]),'ff':int(c[6]),'bram36':int(c[7]),'bram18':int(c[8]),'dsp':int(c[9])}
assert modules['l1i']['bram36']==modules['l1d']['bram36']==4
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
assert abs(periods['pixel_raw']-1000/74.25)<0.001,periods
assert abs(periods['serial_raw']-1000/371.25)<0.001,periods
xdc=read('resolved_constraints.xdc')
uncertainty={}
for line in xdc.splitlines():
    m=re.match(r'set_clock_uncertainty -setup ([0-9.]+) ',line)
    if m:
        for clock in ['cpu_raw','clk_pll_i']:
            if re.search(r'\b'+clock+r'\b',line):uncertainty[clock]=float(m[1])
assert uncertainty=={'cpu_raw':0.3,'clk_pll_i':0.3},uncertainty
manifest=json.loads((synth/'source_manifest.json').read_text())
for rel,value in manifest.items():
    # Exact synthesis inputs, excluding software helpers that can be added later.
    if Path(rel).suffix in {'.sv','.v','.vh','.vhd','.xdc','.prj','.f'} or (rel.startswith('soc/board/') and rel.endswith('.tcl')):
        assert sha(root/rel)==value,rel
mig_inputs=json.loads((synth/'mig_input_hashes.json').read_text(encoding='utf-8-sig'))
for item in mig_inputs:
    assert sha(Path(item['path']))==item['sha256'],item['path']
bit=route/'ORYUKDO-Gen3-BOSIO-KFB.bit'
assert bit.is_file() and bit.stat().st_size>1000000
slacks={name:float(re.search(r'Slack\s+\(MET\)\s*:\s*([-0-9.]+)ns',
          read('timing_'+name+'.rpt'))[1]) for name in ['cpu_raw','clk_pll_i','pixel_raw']}
critical_paths={}
for name in slacks:
    report=read('timing_'+name+'.rpt')
    delay=re.search(r'Data Path Delay:\s*([0-9.]+)ns\s*\(logic ([0-9.]+)ns .*?route ([0-9.]+)ns',report)
    assert delay,('missing critical path delay',name)
    critical_paths[name]={
        'source':re.search(r'^\s*Source:\s*(.+)',report,re.M)[1].strip(),
        'destination':re.search(r'^\s*Destination:\s*(.+)',report,re.M)[1].strip(),
        'data_delay_ns':float(delay[1]),'logic_delay_ns':float(delay[2]),
        'routing_delay_ns':float(delay[3]),
        'logic_levels':int(re.search(r'Logic Levels:\s*(\d+)',report)[1]),
        'slack_ns':slacks[name]}
result={'result':'PASS_IMPLEMENTATION_PHYSICAL_BOARD_UNTESTED','part':'xc7a200tfbg484-2',
 'resources':r,'timing_ns':t,'period_ns':periods,'internal_setup_slack_ns':slacks,
 'critical_paths':critical_paths,
 'module_resources':modules,
 'setup_uncertainty_ns':uncertainty,
 'cdc_warning_critical':0,'known_mig_drc_warnings':warnings,
 'bitstream_sha256':sha(bit),'checkpoint_sha256':sha(route/'routed.dcp'),
 'implementation_input_checkpoint':str(implementation_input),
 'implementation_input_checkpoint_sha256':sha(implementation_input),
 'synthesis_optimized_checkpoint_sha256':sha(synth/'optimized.dcp'),
 'implementation_source_manifest_sha256':sha(synth/'source_manifest.json'),
 'mig_inputs':mig_inputs,
 'synthesis_directory':str(synth),'implementation_directory':str(route),
 'source_match':True,'physical_board_tested':False,
 'fully_routed':True,'unconstrained_internal_endpoints':0,
 'external_io_scope':'Existing asynchronous-input constraints and official TMDS paths; receiver/PCB timing and physical board behavior not validated here.',
 'implementation_scripts_sha256':{p.name:sha(p) for p in (route/'scripts').glob('*.tcl')},
 'raw_tool_critical_warnings':[l for l in read('vivado.log').splitlines() if l.startswith('CRITICAL WARNING:')]}
out.mkdir(parents=True,exist_ok=True)
(out/'board_manifest.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
