#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p validation
verilator --binary --timing --assert -j 4 -Wno-TIMESCALEMOD --top-module tb_l1 \
 --Mdir /var/tmp/haebancheon-l1-16k-test soc/bosio/or_bo_axi_pkg.sv soc/bosio/or_l1_cache.sv \
 soc/bosio/or_l1_cpu.sv soc/tests/axi_ram_model.sv soc/tests/tb_l1.sv > validation/l1_build.log 2>&1
for seed in 1 7 23; do
 /var/tmp/haebancheon-l1-16k-test/Vtb_l1 +seed="$seed" +verilator+seed+"$seed" > "validation/l1_seed$seed.log" 2>&1
done
bash soc/tools/lint.sh > validation/cache_dma3_lint.log 2>&1
