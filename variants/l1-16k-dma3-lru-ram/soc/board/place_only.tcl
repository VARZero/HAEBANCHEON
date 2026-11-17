# Evaluate a physical candidate with the same guards and constraints, stopping
# before a long route. Never emits a board-route PASS marker or a bitstream.
set here [file normalize [file dirname [info script]]]
set fd [open $here/route_guarded.tcl];set guarded [read $fd];close $fd
set anchor {eval $route_script}
if {[string first $anchor $guarded]<0} {error "Guarded route wrapper changed"}
set replacement {
set stop_stage {
report_utilization -file $out/placed_utilization.rpt
report_timing_summary -file $out/placed_timing.rpt
report_control_sets -file $out/control_sets.rpt
puts "PASS PLACEMENT_ONLY_NOT_ROUTED"
exit
}
if {[string first "\nroute_design\n" $route_script]<0} {error "Route stage not found"}
set route_script [string map [list "\nroute_design\n" "\n$stop_stage\n"] $route_script]
eval $route_script
}
eval [string map [list $anchor $replacement] $guarded]
