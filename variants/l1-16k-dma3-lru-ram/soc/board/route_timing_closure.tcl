# Preserve existing placement, but target WNS=0 rather than copying a negative
# reference WNS. Clock periods, 0.3 ns setup reserve and audits are unchanged.
# AMD Vivado 2022.2 UG835: read_checkpoint -directive TimingClosure.
set fd [open [file join [file dirname [info script]] route_incremental.tcl]]
set script [read $fd]
close $fd
set anchor {read_checkpoint -incremental $reference}
if {[string first $anchor $script]<0} {error "Incremental loading stage changed"}
set script [string map [list $anchor {read_checkpoint -incremental -directive TimingClosure $reference}] $script]
eval $script
