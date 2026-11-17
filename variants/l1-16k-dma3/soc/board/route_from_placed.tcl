# Resume an already guarded, placed/phys-opt checkpoint. All board preflight,
# timing, DRC/CDC, route-completeness audits and bitstream gates remain intact.
set here [file normalize [file dirname [info script]]]
set fd [open $here/route_board.tcl];set script [read $fd];close $fd
set stages {place_design
phys_opt_design
route_design}
if {[string first $stages $script]<0} {error "Base route stages changed"}
set script [string map [list $stages {route_design}] $script]
eval $script
