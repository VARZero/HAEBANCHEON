# The first routed 16 KiB design had a 3841-load projector enable net with
# 9.55 ns net delay. Replicate only its driver using the placed LRU candidate.
# No RTL, clock period, pipeline stage or timing exception changes.
set_param general.maxThreads 2
set input [file normalize [lindex $argv 0]]
set out [file normalize [lindex $argv 1]]
file mkdir $out
open_checkpoint $input
set target_name {system/bosio/output0/u_projector/E[0]}
set nets [get_nets -hier -filter [format {NAME == "%s"} $target_name]]
if {[llength $nets]!=1} {error "Observed projector enable net not found: $target_name"}
set loads [get_pins -leaf -of_objects $nets -filter {DIRECTION == IN}]
set drivers [get_pins -leaf -of_objects $nets -filter {DIRECTION == OUT}]
if {[llength $loads]<512 || [llength $drivers]!=1} {error "Unexpected target connectivity"}
set driver [get_cells -of_objects $drivers]
if {![string match LUT* [get_property REF_NAME $driver]]} {error "Expected combinational LUT driver"}
set fd [open $out/target.txt w]
puts $fd "net=$nets\ndriver=$driver\nloads_before=[llength $loads]"
close $fd
set_property FORCE_MAX_FANOUT 128 $nets
phys_opt_design -force_replication_on_nets $nets
report_high_fanout_nets -load_types -max_nets 30 -file $out/fanout.rpt
report_utilization -file $out/utilization.rpt
report_timing_summary -file $out/timing.rpt
report_drc -no_waivers -file $out/drc.rpt
set fd [open $out/drc.rpt];set drc [read $fd];close $fd
if {[regexp -line {^[A-Z0-9-]+#[0-9]+ (Error|Critical Warning)} $drc] ||
    [regexp -line {^(DPIP-1|DPOP-1|DPOP-2|REQP-1839|REQP-1840|RBOR-1)#[0-9]+} $drc]} {
 error "Targeted fanout optimization introduced DRC violations"
}
write_checkpoint -force $out/fanout_optimized.dcp
puts "TARGETED_PROJECTOR_ENABLE_PREPARED_NOT_ROUTED"
close_design
exit
