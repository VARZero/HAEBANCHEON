# Local physical experiment for the exact RS FIFO identified by RQS_CONG-2_1.
# MUXF_REMAP changes combinational mapping only; no RTL/latency/clock changes.
set_param general.maxThreads 4
set input [file normalize [lindex $argv 0]]
set out [file normalize [lindex $argv 1]]
file mkdir $out
open_checkpoint $input
set prefix {system/soc/cpu/scheduler/U_READY_STATION/genblk1[1].U_RS_FIFO/}
proc scoped_muxes {prefix} {
 set result {}
 foreach cell [get_cells -hier -filter {REF_NAME =~ MUXF*}] {
  if {[string first $prefix $cell]==0} {lappend result $cell}
 }
 return $result
}
set targets [scoped_muxes $prefix]
set initial [llength $targets]
set all_muxes [get_cells -hier -filter {REF_NAME =~ MUXF*}]
set all_initial [llength $all_muxes]
if {$initial==0} {error "Observed congested RS FIFO MUXF scope not found"}
set fd [open $out/remapped_mux_cells.txt w]
foreach cell $targets {puts $fd $cell}
close $fd
set_property MUXF_REMAP false $all_muxes
set_property MUXF_REMAP true $targets
# Run only this optimization, excluding every MUXF outside the observed FIFO.
# In particular, do not rerun the default power/BRAM optimization sequence.
opt_design -muxf_remap
set remaining [llength [scoped_muxes $prefix]]
if {$remaining>=$initial} {error "Scoped MUXF remap made no mapping change"}
if {[llength [get_cells -hier -filter {REF_NAME =~ MUXF*}]]!=$all_initial-$initial+$remaining} {
 error "MUXF cells outside the requested scope changed"
}
report_utilization -file $out/optimized_utilization.rpt
report_drc -no_waivers -file $out/optimized_drc.rpt
set fd [open $out/optimized_drc.rpt];set drc [read $fd];close $fd
if {[regexp -line {^[A-Z0-9-]+#[0-9]+ (Error|Critical Warning)} $drc] ||
    [regexp -line {^(DPIP-1|DPOP-1|DPOP-2|REQP-1839|REQP-1840|RBOR-1)#[0-9]+} $drc]} {
 error "Scoped physical experiment introduced DRC violations"
}
write_checkpoint -force $out/optimized.dcp
puts "PASS SCOPED_RS_MUX_REMAP before=$initial after=$remaining"
close_design
exit
