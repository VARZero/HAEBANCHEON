`timescale 1ns/1ps
module tb_l1;
 import or_bo_axi_pkg::*;
 bit clk=0,reset_n=0;always #5 clk=~clk;
 logic icache_flush=0,invalidate=0;logic [31:0] invalidate_address=0;
 logic if_valid=0,if_response_ready=0,data_valid=0,data_response_ready=0,data_store=0;
 logic [31:0] if_address=0,data_address=0,data_write=0;logic [3:0] data_mask=15;
 wire if_ready,if_response_valid,data_ready,data_response_valid,data_fault;
 wire [95:0] if_data;wire [2:0] if_fault;wire [31:0] data_read;
 ax_req_t req;ax_rsp_t rsp;
 or_l1_cpu #(.UNCACHED_BASE(32'h80010000),.UNCACHED_LIMIT(32'h80018000)) dut(.*);
 axi_ram_model #(.WORDS(32768)) ram(.*);
 int cycles=0,checks=0,start_reads;
 logic [31:0] a,v;logic [3:0] m;
 always @(posedge clk)begin cycles<=cycles+1;if(cycles>2000000)$fatal(1,"cache timeout");end
 task automatic access(input bit wr,input logic [31:0] addr,input logic [31:0] value=0,input logic [3:0] mask=15,input bit check_value=1);
  @(negedge clk);data_valid=1;data_store=wr;data_address=addr;data_write=value;data_mask=mask;
  do @(posedge clk);while(!data_ready);
  @(negedge clk);data_valid=0;
  while(!data_response_valid)@(negedge clk);
  if(check_value)begin
   assert(!data_fault)else $fatal(1,"fault at %x",addr);
   if(!wr)assert(data_read==ram.mem[(addr-32'h80000000)>>2])else $fatal(1,"stale read %x got=%x expected=%x",addr,data_read,ram.mem[(addr-32'h80000000)>>2]);
   else for(int b=0;b<4;b++)if(mask[b])assert(ram.mem[(addr-32'h80000000)>>2][8*b+:8]==value[8*b+:8])else $fatal(1,"store acknowledged before DDR");
  end
  repeat(3)@(negedge clk);
  data_response_ready=1;@(negedge clk);data_response_ready=0;checks++;
 endtask
 task automatic fetch(input logic [31:0] addr,input bit hold_response=0);
  @(negedge clk);if_valid=1;if_address=addr;
  do @(posedge clk);while(!if_ready);
  @(negedge clk);if_valid=0;while(!if_response_valid)@(negedge clk);
  for(int i=0;i<3;i++)assert(!if_fault[i] && if_data[32*i+:32]==ram.mem[((addr-32'h80000000)>>2)+i])else $fatal(1,"fetch mismatch %x",addr);
  if(!hold_response)begin if_response_ready=1;@(negedge clk);if_response_ready=0;end
 endtask
 task automatic snoop(input logic [31:0] addr,input logic [31:0] value);
  @(negedge clk);invalidate=1;invalidate_address=addr;
  @(negedge clk);invalidate=0;ram.mem[(addr-32'h80000000)>>2]=value;
 endtask
 initial begin
  repeat(5)@(negedge clk);for(int i=0;i<32768;i++)ram.mem[i]=32'h12000000+32'(i);reset_n=1;
  // Both ways, all 256 sets: every one of 512 lines must remain resident.
  for(int i=0;i<512;i++)access(0,32'h80000000+32'(i*32));
  start_reads=ram.reads;
  for(int i=0;i<512;i++)access(0,32'h80000000+32'(i*32+28));
  assert(ram.reads==start_reads)else $fatal(1,"D capacity below 16 KiB/two ways");
  for(int i=0;i<512;i++)fetch(32'h80000000+32'(i*32));
  start_reads=ram.reads;
  for(int i=0;i<512;i++)fetch(32'h80000000+32'(i*32+4));
  assert(ram.reads==start_reads)else $fatal(1,"I capacity below 16 KiB/two ways");
  fetch(32'h80000ffc);fetch(32'h8000001c);
  access(0,32'h80000000);access(0,32'h80002000);access(0,32'h80000000);access(0,32'h80004000);
  start_reads=ram.reads;access(0,32'h80000000);assert(ram.reads==start_reads)else $fatal(1,"LRU chose recent way");
  start_reads=ram.reads;access(0,32'h80002000);assert(ram.reads>start_reads)else $fatal(1,"third tag did not evict");
  access(1,32'h80000000,32'hcafebabe,4'b0101);access(0,32'h80000000);fetch(32'h80000000);
  snoop(32'h80000000,32'hfeed1234);access(0,32'h80000000);fetch(32'h80000000);
  start_reads=ram.reads;access(0,32'h80010000);access(0,32'h80010000);
  assert(ram.reads==start_reads+2)else $fatal(1,"uncached region allocated or burst");
  // A blocked I response must leave the D path live.
  fetch(32'h80000020,1);access(0,32'h80000100);access(1,32'h80000100,32'hface0001);
  assert(if_response_valid)else $fatal(1,"fetch response lost");
  if_response_ready=1;@(negedge clk);if_response_ready=0;
  // Full invalidation must kill cache installation from an in-flight fill.
  @(negedge clk);icache_flush=1;@(negedge clk);icache_flush=0;
  fork
   fetch(32'h80000100);
   begin wait(ram.reading);@(negedge clk);icache_flush=1;@(negedge clk);icache_flush=0;
    wait(dut.instruction_cache.response_valid);@(negedge clk);
    assert(!dut.instruction_cache.valid0[8] && !dut.instruction_cache.valid1[8])else $fatal(1,"flush resurrected in-flight refill");
   end
  join
  // A later instruction of the same bundle may legitimately refill after FENCE.
  fetch(32'h80000100);
  // Invalidation concurrent with a D refill must also prevent stale installation.
  snoop(32'h80000200,32'h12345678);
  fork
   access(0,32'h80000200,0,15,0);
   begin wait(ram.reading);snoop(32'h80000200,32'h99887766);end
  join
  access(0,32'h80000200);
  for(int i=0;i<4000;i++)begin
   a=32'h80000000+($urandom_range(0,32767)<<2);v=$urandom;m=4'($urandom_range(1,15));
   case(i%7)
    0:snoop(a,v);
    1,2:access(1,a,v,m);
    default:access(0,a);
   endcase
  end
  access(0,32'h10009000,0,15,0);assert(data_fault)else $fatal(1,"uncached AXI error swallowed");
  // Reset invalidates metadata without clearing cache BRAM.
  @(negedge clk);reset_n=0;repeat(5)@(negedge clk);reset_n=1;
  ram.mem[0]=32'hdeadbeef;access(0,32'h80000000);fetch(32'h80000000);
  $display("PASS L1_16K_2WAY capacity I/D, LRU, byte stores, DMA visibility, refill races, independent responses, errors, random checks=%0d cycles=%0d",checks,cycles);$finish;
 end
endmodule
