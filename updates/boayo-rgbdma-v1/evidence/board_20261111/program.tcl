open_hw_manager
connect_hw_server -url localhost:3121
set targets [get_hw_targets -quiet */Digilent/210251249581]
if {[llength $targets] != 1} {error "Expected one identified Digilent target"}
current_hw_target [lindex $targets 0]
open_hw_target
set devices [get_hw_devices -quiet]
if {[llength $devices] != 1} {error "Expected one FPGA"}
set dev [lindex $devices 0]
if {[get_property PART $dev] ne "xc7a200t"} {error "Unexpected FPGA part"}
current_hw_device $dev
set_property PROGRAM.FILE {D:/temp/gen3bosio/sd4-dma/candidate/ORYUKDO-KFB-SD4-DMA-v1.bit} $dev
program_hw_devices $dev
refresh_hw_device $dev
puts "CONFIG_DONE=[get_property REGISTER.CONFIG_STATUS.BIT14_DONE_PIN $dev]"
puts "CRC_ERROR=[get_property REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR $dev]"
puts "PROGRAM_COMPLETED"
close_hw_target
disconnect_hw_server
close_hw_manager

