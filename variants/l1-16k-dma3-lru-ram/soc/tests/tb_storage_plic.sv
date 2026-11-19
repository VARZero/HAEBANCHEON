`timescale 1ns/1ps
module tb_storage_plic;
 import or_bo_axi_pkg::*;
 logic clk=0,reset_n=0;always #5 clk=~clk;
 logic [5:0] sources=0;al_req_t req;al_rsp_t rsp;wire m_irq,s_irq;
 or_storage_plic #(.SOURCES(6)) dut(.*);
 task automatic put(input logic [31:0] a,v);
  bit ah,wh;ah=0;wh=0;@(negedge clk);req='0;req.awaddr=a;req.wdata=v;req.wstrb=15;req.awvalid=1;req.wvalid=1;
  while(!ah || !wh) begin
   @(posedge clk);if(req.awvalid && rsp.awready)ah=1;if(req.wvalid && rsp.wready)wh=1;
   @(negedge clk);req.awvalid=!ah;req.wvalid=!wh;
  end
  req.bready=1;do @(posedge clk);while(!rsp.bvalid);assert(rsp.bresp==OKAY);
  @(negedge clk);req='0;
 endtask
 task automatic get(input logic [31:0] a,v);
  @(negedge clk);req='0;req.araddr=a;req.rmask=15;req.arvalid=1;
  do @(posedge clk);while(!rsp.arready);@(negedge clk);req.arvalid=0;
  // Claim side effect must occur once even with a stalled response.
  repeat(5)@(negedge clk);req.rready=1;
  do @(posedge clk);while(!rsp.rvalid);
  assert(rsp.rdata==v && rsp.rresp==OKAY)else $fatal(1,"PLIC %h got %h expected %h",a,rsp.rdata,v);
  @(negedge clk);req='0;
 endtask
 initial begin
  req='0;repeat(5)@(negedge clk);reset_n=1;
  put('h4,1);put('h8,2);put('h2080,6);sources=3;repeat(3)@(negedge clk);
  assert(s_irq && !m_irq);get('h201004,2);get('h201004,1);
  assert(!s_irq);get('h201004,0);
  // Completing through the wrong context cannot release a claimed source.
  put('h200004,2);repeat(3)@(negedge clk);assert(!s_irq);
  put('h201004,2);repeat(3)@(negedge clk);assert(s_irq);get('h201004,2);
  sources=0;put('h201004,1);put('h201004,2);repeat(3)@(negedge clk);assert(!s_irq);
  put('h2000,6);put('h2080,0);put('h200000,2);sources=3;repeat(3)@(negedge clk);assert(!m_irq);
  put('h200000,0);get('h200004,2);sources=0;get('h200004,1);put('h200004,2);put('h200004,1);
  // Both projectors can finish together while DMA source 4 is pending.
  put('h10,1);put('h14,1);put('h18,1);put('h2080,'h70);
  sources='h38;repeat(3)@(negedge clk);get('h1000,'h70);
  get('h201004,4);get('h201004,5);get('h201004,6);get('h201004,0);
  sources=0;
  put('h201004,4);put('h201004,5);put('h201004,6);
  get('h201004,0);assert(!s_irq);
  sources[5]=1;repeat(3)@(negedge clk);get('h201004,6);
  sources=0;put('h201004,6);get('h201004,0);
  $display("PASS PLIC M/S contexts, sources 4–6, level re-pending and claim/complete");$finish;
 end
 initial begin #100000;$fatal(1,"timeout");end
endmodule
