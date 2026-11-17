`timescale 1ns/1ps
module tb_component_scenes;
 reg clk=0;always #5 clk=~clk;
 reg rst_n=0,request=0,frame_start=0,sample_valid=0;
 reg [31:0] base=32'h01000000;
 reg [12:0] directory_addr=0;reg [9:0] cell_addr=0;
 wire scene_valid,pending,busy,error,sample_ready,frame_ready,pixel_valid;
 wire [23:0] pixel;wire [31:0] received,araddr,active_scene_base,hits,misses,stall_cycles;
 wire [7:0] arlen;wire arvalid,rready;
 reg transfer=0;reg [31:0] held;reg [7:0] beat,total;integer cycle=0;
 reg [31:0] memory[0:1084799];
 wire arready=!transfer;wire rvalid=transfer && cycle%3!=0;wire rlast=beat==total;
 wire [31:0] offset=((held & 32'h00ffffff)>>2)+{24'b0,beat};
 wire [31:0] rdata=memory[offset];
 always @(posedge clk)begin
  cycle<=cycle+1;
  if(arvalid&&arready)begin transfer<=1;held<=araddr;beat<=0;total<=arlen;end
  if(rvalid&&rready)begin if(rlast)transfer<=0;else beat<=beat+1'b1;end
 end
 bosio_ddr_scene_cache #(.CACHE_BYTES(256),.QUEUE_BITS(10)) dut(
  .clk,.rst_n,.request,.patch_request(1'b0),.base,.word_count(32'd1084800),.frame_start,
  .scene_valid,.pending,.busy,.error,.received,.araddr,.arlen,.arvalid,.arready,
  .rdata,.rresp(2'b00),.rlast,.rvalid,.rready,.sample_valid,.directory_addr,.cell_addr,
  .sample_ready,.frame_ready,.output_ready(1'b1),.pixel_valid,.pixel,
  .hits,.misses,.stall_cycles,.active_scene_base);
 task automatic sample(input integer tile,input integer cell_index);
  integer n,index;reg[31:0] directory,packed_pixel;reg[23:0] expected;
  begin
   directory=memory[256+tile];expected=0;
   if(directory!=32'hffffffff && {1'b0,directory}+cell_index<(1084800-4476)*4)begin
    packed_pixel=memory[4476+((directory+cell_index)>>2)];index=(packed_pixel>>(8*((directory+cell_index)%4)))&255;
    expected=memory[index][23:0];
   end
   @(negedge clk);while(!sample_ready)@(negedge clk);
   directory_addr=13'(tile);cell_addr=10'(cell_index);sample_valid=1;
   @(negedge clk);sample_valid=0;n=0;
   while(!pixel_valid)begin @(negedge clk);n++;if(n>2000)$fatal(1,"sample timeout");end
   if(pixel!==expected)$fatal(1,"tile=%0d cell_index=%0d got=%h expected=%h",tile,cell_index,pixel,expected);
   @(negedge clk);while(!frame_ready)@(negedge clk);
  end
 endtask
 string root,path;integer phase,tile,n;
 initial begin
  if(!$value$plusargs("root=%s",root))$fatal(1,"root required");
  repeat(5)@(negedge clk);rst_n=1;
  for(phase=0;phase<3;phase++)begin
   case(phase)
    0:path={root,"/dashboard_and_controls.hex"};
    1:path={root,"/three_windows.hex"};
    2:path={root,"/close_telemetry_reveal.hex"};
   endcase
   $readmemh(path,memory);
   @(negedge clk);base=phase%2?32'h02000000:32'h01000000;request=1;
   @(negedge clk);request=0;n=0;
   while(!pending)begin @(negedge clk);n++;if(n>25000)$fatal(1,"metadata timeout");end
   if(!frame_ready)$fatal(1,"output not drained");
   frame_start=1;@(negedge clk);frame_start=0;
   if(!scene_valid||error||busy||active_scene_base!=base)$fatal(1,"commit failed");
   for(tile=0;tile<4220;tile++)begin sample(tile,0);sample(tile,511);sample(tile,1023);end
   $display("PASS_COMPONENT_SCENE phase=%0d samples=12660",phase);
  end
  $display("PASS_COMPONENT_SCENES 3 scenes 37980 sampled cells");$finish;
 end
 initial begin #100000000;$fatal(1,"global timeout");end
endmodule
