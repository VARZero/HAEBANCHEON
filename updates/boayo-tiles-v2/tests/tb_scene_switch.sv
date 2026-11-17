`timescale 1ns/1ps
module tb_scene_switch;
 reg clk=0;always #5 clk=~clk;
 reg rst_n=0,request=0,patch_request=0,frame_start=0,sample_valid=0;
 reg [31:0] base=32'h10000,word_count=4752;
 reg [12:0] directory_addr=0;reg [9:0] cell_addr=0;
 wire scene_valid,pending,busy,error,sample_ready,frame_ready,pixel_valid;
 wire [23:0] pixel;wire [31:0] received,araddr,active_scene_base,hits,misses,stall_cycles;
 wire [7:0] arlen;wire arvalid,rready;
 reg transfer=0;reg [31:0] held;reg [7:0] beat,total;integer cycle=0,metadata_beats=0;
 reg inject_error=0;reg [7:0] data0=1;
 wire arready=!transfer;
 wire rvalid=transfer && (cycle%3!=0);
 wire rlast=beat==total;
 wire [31:0] address=held+{22'b0,beat,2'b0};
 wire [31:0] offset=(address>=32'h20000 ? address-32'h20000:address-32'h10000)>>2;
 reg [31:0] rdata;
 always @* begin
  if(offset<256)case(offset)
   1:rdata=32'h00112233;2:rdata=32'h00445566;3:rdata=32'h00778899;default:rdata=0;
  endcase
  else if(offset<4476)rdata=offset==256?0:32'hffffffff;
  else rdata=address>=32'h20000 ? 32'h02020202:{4{data0}};
 end
 wire [1:0] rresp=inject_error?2'b10:2'b00;
 always @(posedge clk)begin
  cycle<=cycle+1;
  if(arvalid&&arready)begin transfer<=1;held<=araddr;beat<=0;total<=arlen;end
  if(rvalid&&rready)begin
   if(offset<4480)metadata_beats<=metadata_beats+1;
   if(rlast)transfer<=0;else beat<=beat+1'b1;
  end
 end
 bosio_ddr_scene_cache #(.CACHE_BYTES(256),.QUEUE_BITS(10)) dut(
  .clk,.rst_n,.request,.patch_request,.base,.word_count,.frame_start,
  .scene_valid,.pending,.busy,.error,.received,.araddr,.arlen,.arvalid,.arready,
  .rdata,.rresp,.rlast,.rvalid,.rready,.sample_valid,.directory_addr,.cell_addr,
  .sample_ready,.frame_ready,.output_ready(1'b1),.pixel_valid,.pixel,
  .hits,.misses,.stall_cycles,.active_scene_base);
 task automatic command(input bit patch,input [31:0] addr);
  @(negedge clk);base=addr;request=!patch;patch_request=patch;
  @(negedge clk);request=0;patch_request=0;
 endtask
 task automatic wait_pending;
  integer n;begin n=0;while(!pending)begin @(negedge clk);n++;if(n>25000)$fatal(1,"pending timeout");end end
 endtask
 task automatic commit(input [31:0] expected);
  @(negedge clk);if(!frame_ready)$fatal(1,"not drained");frame_start=1;
  @(negedge clk);frame_start=0;
  if(active_scene_base!=expected || !scene_valid || busy || error)$fatal(1,"commit state");
 endtask
 task automatic sample(input [12:0] tile,input [23:0] expected);
  integer n;begin
   @(negedge clk);directory_addr=tile;sample_valid=1;
   @(negedge clk);sample_valid=0;n=0;
   while(!pixel_valid)begin @(negedge clk);n++;if(n>2000)$fatal(1,"pixel timeout");end
   if(pixel!==expected)$fatal(1,"pixel mismatch got %h expected %h",pixel,expected);
   @(negedge clk);while(!frame_ready)@(negedge clk);
  end
 endtask
 integer before_reads;
 initial begin
  repeat(5)@(negedge clk);rst_n=1;
  command(0,32'h10000);wait_pending();
  if(received!=4480 || scene_valid)$fatal(1,"first metadata load");
  repeat(7)@(negedge clk);commit(32'h10000);sample(0,24'h112233);sample(1,0);
  before_reads=received;
  command(1,32'h20000);wait_pending();
  if(received!=before_reads || active_scene_base!=32'h10000)$fatal(1,"patch loaded or switched early");
  sample(0,24'h112233);commit(32'h20000);sample(0,24'h445566);
  data0=3;command(1,32'h10000);wait_pending();commit(32'h10000);sample(0,24'h778899);
  if(received!=before_reads)$fatal(1,"patch metadata traffic");
  command(0,32'h20000);wait_pending();commit(32'h20000);
  if(received!=before_reads+4480)$fatal(1,"reload count");
  inject_error=1;command(0,32'h10000);
  repeat(24000)@(negedge clk);
  if(!error || pending || busy || active_scene_base!=32'h20000)$fatal(1,"failed metadata published");
  inject_error=0;command(0,32'h10000);wait_pending();commit(32'h10000);sample(0,24'h778899);
  $display("PASS BS25 actual RTL: full/patch, frame boundary, old reads drain, cache invalidate, inactive black, AXI stalls, metadata error recovery");$finish;
 end
 initial begin #2000000;$fatal(1,"watchdog");end
endmodule
