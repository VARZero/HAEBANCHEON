"""Collect PASS evidence from named runs; never infer completion from file presence."""
import hashlib,json,re,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
checks=[]
def check(relative,marker):
    p=root/relative;raw=p.read_bytes();s=raw.decode('utf-8',errors='replace')
    assert marker in s and not re.search(r'(%Error|Assertion failed|FATAL)',s),str(p)
    checks.append({'log':relative,'sha256':hashlib.sha256(raw).hexdigest(),
                   'pass_lines':[x for x in s.splitlines() if x.startswith('PASS ')]})
for seed in [1,7,23]:
    check(f'validation/l1_seed{seed}.log','PASS L1_16K_2WAY')
    for prefix in ['cpu_storage','cpu_storage_native']:
        check(f'validation/{prefix}_seed{seed}.log','PASS THREE_DMA')
    for backend in ['axi','native']:
        check(f'validation/cpu_sv32_{backend}_seed{seed}.log','PASS CPU_SV32_CACHE')
    for prefix in ['', 'native_']:
        for scene in ['', '_patterned']:
            check(f'soc/reports/{prefix}integration{scene}_seed{seed}.log','PASS GEN3_BOSIO')
    for unit in ['dma','plic','irq']:
        check(f'validation/{unit}_seed{seed}.log','PASS')
for name in ['native_test','native_delayed_input_test','storage_test']:
    check(f'validation/{name}.log','PASS')
for mode in ['header','oversize','payload-crc','card-crc','ddr']:
    check(f'validation/boot_reject_{mode}.log','PASS')
lint=(root/'soc/reports/lint.log').read_text(errors='replace')
assert not re.search(r'%(Error|Warning-(LATCH|MULTIDRIVEN|UNOPTFLAT))',lint)
new_warnings=[x for x in lint.splitlines() if x.startswith('%Warning') and
              re.search(r'(or_l1_|or_storage_plic|gen3_bosio_soc|or_bo_axi_pkg)',x)]
assert not new_warnings,new_warnings
result={'result':'PASS_RTL_REGRESSIONS','checks':checks,'lint_errors_latches_multidrivers_comb_loops':0,
        'cpu_firmware_sha256':{p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (root/'validation').glob('cpu_*') if p.suffix in {'.elf','.hex','.bin'}},
        'sv32_test_source_sha256':hashlib.sha256((root/'soc/tests/cpu_cache_sv32.c').read_bytes()).hexdigest(),
        'new_cache_dma_interface_lint_warnings':new_warnings,
        'linux_full_cpu_rtl_boot':False,'physical_board_test':False,
        'ddr_backend':'AXI RAM and production AXI-MIG adapter/native arbiter with mock DDR'}
(out/'validation_summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS RTL evidence logs:',len(checks))
