#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p validation provenance
verilator --binary --timing --assert -j 2 -Wno-TIMESCALEMOD --top-module tb_l1 \
 --Mdir /var/tmp/haebancheon-lru-equivalence soc/bosio/or_bo_axi_pkg.sv \
 soc/bosio/or_l1_cache.sv soc/bosio/or_l1_cpu.sv \
 soc/tests/lru_reference/or_l1_cache_reference.sv soc/tests/lru_reference/or_l1_cpu_reference.sv \
 soc/tests/axi_ram_model.sv soc/tests/lru_reference/tb_l1.sv > validation/lru_equivalence_build.log 2>&1
for seed in 1 7 23; do
 /var/tmp/haebancheon-lru-equivalence/Vtb_l1 +seed="$seed" +verilator+seed+"$seed" \
  > "validation/lru_equivalence_seed$seed.log" 2>&1
done
python3 - <<'PY'
import hashlib,json
from pathlib import Path
result={'result':'PASS_CYCLE_EQUIVALENCE','initial_lru':'candidate all ones, reference reset zeros','logs':{}}
for seed in [1,7,23]:
 p=Path(f'validation/lru_equivalence_seed{seed}.log');raw=p.read_bytes()
 assert b'PASS L1_16K_2WAY' in raw and b'Error' not in raw
 result['logs'][str(p)]=hashlib.sha256(raw).hexdigest()
sources=[Path('soc/bosio/or_l1_cache.sv'),Path('soc/bosio/or_l1_cpu.sv')]
sources+=list(Path('soc/tests/lru_reference').glob('*.sv'))
result['sources_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
Path('provenance/lru_equivalence.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS LRU every-cycle equivalence: three seeds, full capacity, coherence, reset, errors')
PY
