// Independent I/D responses: a stalled fetch cannot block an older data load.
module or_l1_cpu #(
 parameter logic [31:0] UNCACHED_BASE=32'hbd000000,UNCACHED_LIMIT=32'hbe000000
)(
 input wire clk,reset_n,icache_flush,
 input wire invalidate,input wire [31:0] invalidate_address,
 input wire if_valid,output wire if_ready,input wire [31:0] if_address,
 output wire if_response_valid,input wire if_response_ready,
 output logic [95:0] if_data,output logic [2:0] if_fault,
 input wire data_valid,output wire data_ready,input wire data_store,
 input wire [31:0] data_address,data_write,input wire [3:0] data_mask,
 output wire data_response_valid,input wire data_response_ready,
 output wire [31:0] data_read,output wire data_fault,
 output or_bo_axi_pkg::ax_req_t req,input or_bo_axi_pkg::ax_rsp_t rsp
);
 import or_bo_axi_pkg::*;
 typedef enum logic [1:0] {IF_IDLE,IF_REQ,IF_WAIT,IF_DONE} if_state_t;
 if_state_t if_state;
 logic [31:0] pc;logic [1:0] index;
 wire ir,iv;wire [31:0] idata;wire ifault;
 wire local_inv=data_valid && data_ready && data_store && (|data_mask);
 ax_req_t requests[2];ax_rsp_t responses[2];
 or_l1_cache #(.READ_ONLY(1),.UNCACHED_BASE(UNCACHED_BASE),.UNCACHED_LIMIT(UNCACHED_LIMIT)) instruction_cache(.clk,.reset_n,.flush(icache_flush),
  .valid(if_state==IF_REQ),.ready(ir),.store(1'b0),.address(pc),.write_data(32'b0),.mask(4'hf),
  .response_valid(iv),.response_ready(if_state==IF_WAIT),.read_data(idata),.fault(ifault),
  .invalidate,.invalidate_address,.local_invalidate(local_inv),.local_address(data_address),.req(requests[0]),.rsp(responses[0]));
 or_l1_cache #(.UNCACHED_BASE(UNCACHED_BASE),.UNCACHED_LIMIT(UNCACHED_LIMIT)) data_cache(.clk,.reset_n,.flush(1'b0),
  .valid(data_valid),.ready(data_ready),.store(data_store),.address(data_address),.write_data(data_write),.mask(data_mask),
  .response_valid(data_response_valid),.response_ready(data_response_ready),.read_data(data_read),.fault(data_fault),
  .invalidate,.invalidate_address,.local_invalidate(1'b0),.local_address(32'b0),.req(requests[1]),.rsp(responses[1]));
 assign if_ready=reset_n && if_state==IF_IDLE;
 assign if_response_valid=reset_n && if_state==IF_DONE;
 always_ff @(posedge clk)begin
  if(!reset_n)begin if_state<=IF_IDLE;pc<=0;index<=0;if_data<=0;if_fault<=0;end
  else case(if_state)
   IF_IDLE:if(if_valid)begin pc<=if_address;index<=0;if_state<=IF_REQ;end
   IF_REQ:if(ir)if_state<=IF_WAIT;
   IF_WAIT:if(iv)begin
    if_data[32*index+:32]<=idata;if_fault[index]<=ifault;
    if(index==2)if_state<=IF_DONE;
    else begin index<=index+1'b1;pc<=pc+4;if_state<=IF_REQ;end
   end
   IF_DONE:if(if_response_ready)if_state<=IF_IDLE;
  endcase
 end
 // AXI owner is held until B or the final R handshake. No response buffering
 // depends on the consumer of the assembled three-instruction bundle.
 logic busy,owner,turn;
 wire want0=requests[0].arvalid || requests[0].awvalid;
 wire want1=requests[1].arvalid || requests[1].awvalid;
 always_comb begin
  req='0;responses[0]='0;responses[1]='0;
  if(busy)begin req=requests[owner];responses[owner]=rsp;end
 end
 always_ff @(posedge clk)begin
  if(!reset_n)begin busy<=0;owner<=0;turn<=1;end
  else if(!busy)begin
   if(want0 || want1)begin owner<=want1 && (!want0 || turn);busy<=1;end
  end else if((req.rready && rsp.rvalid && rsp.r.last) || (req.bready && rsp.bvalid))begin
   busy<=0;turn<=!owner;
  end
 end
endmodule
