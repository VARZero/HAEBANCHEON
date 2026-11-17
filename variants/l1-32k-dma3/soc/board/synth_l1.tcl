set root [file normalize [file dirname [info script]]/../..]
set out [file normalize [lindex $argv 0]]
file mkdir $out
cd $out
set_param general.maxThreads 6
create_project -in_memory -part xc7a200tfbg484-2
read_verilog -sv $root/soc/bosio/or_bo_axi_pkg.sv
read_verilog -sv $root/soc/bosio/or_l1_cache.sv
read_verilog -sv $root/soc/bosio/or_l1_cpu.sv
synth_design -top or_l1_cpu -part xc7a200tfbg484-2 -mode out_of_context
create_clock -period 20 [get_ports clk]
report_utilization -file $out/utilization.rpt
report_utilization -hierarchical -file $out/hierarchy.rpt
report_timing_summary -file $out/timing.rpt
report_drc -file $out/drc.rpt
write_checkpoint -force $out/l1.dcp
set rams [get_cells -hier -filter {REF_NAME =~ RAMB*}]
if {[llength $rams]<16} {error "L1 cache payload did not infer expected BRAM capacity"}
puts "PASS L1 BRAM_SYNTHESIS rams=[llength $rams]"
