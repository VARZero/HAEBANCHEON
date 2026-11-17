// Physical 32 KiB, two-way, 32-byte lines. Write-through/no-write-allocate:
// a store response is not returned until DDR acknowledges the write.
// Two independent invalidation inputs cover fabric DMA and local CPU stores.
module or_l1_cache #(
 parameter bit READ_ONLY=0,
 parameter logic [31:0] UNCACHED_BASE=32'hbd000000, UNCACHED_LIMIT=32'hbe000000
)(
 input wire clk,reset_n,flush,
 input wire valid,output wire ready,input wire store,
 input wire [31:0] address,write_data,input wire [3:0] mask,
 output wire response_valid,input wire response_ready,
 output logic [31:0] read_data,output logic fault,
 input wire invalidate,input wire [31:0] invalidate_address,
 input wire local_invalidate,input wire [31:0] local_address,
 output or_bo_axi_pkg::ax_req_t req,input or_bo_axi_pkg::ax_rsp_t rsp
);
 import or_bo_axi_pkg::*;
 typedef enum logic [3:0] {IDLE,LOOKUP,AR,R,WRITE,B,DONE} state_t;
 state_t state;
 logic [31:0] saved_address,saved_write;
 logic [3:0] saved_mask;
 logic cacheable,refill,way,refill_killed,read_error,aw_done,w_done;
 logic [2:0] beat;
 (* ram_style="block" *) logic [31:0] data0[4096],data1[4096];
 (* ram_style="distributed" *) logic [17:0] tags0[512],tags1[512];
 logic [511:0] valid0,valid1,lru;
 logic [31:0] rd0,rd1;
 wire [8:0] set_index=saved_address[13:5];
 wire [17:0] tag=saved_address[31:14];
 wire [11:0] read_index=state==IDLE?address[13:2]:saved_address[13:2];
 wire inv_match=(invalidate && invalidate_address[31:5]==saved_address[31:5]) ||
                (local_invalidate && local_address[31:5]==saved_address[31:5]);
 wire hit0=valid0[set_index] && tags0[set_index]==tag;
 wire hit1=valid1[set_index] && tags1[set_index]==tag;
 wire read_beat=state==R && rsp.rvalid;
 logic ram_write,ram_way;
 logic [11:0] ram_index;
 logic [31:0] ram_payload;
 // Register the refill write port. AXI response valid includes the CDC reset
 // gate; it must not feed BRAM WE asynchronously. The final write completes
 // while DONE is consumed, before the next request can perform its lookup.
 always_ff @(posedge clk)begin
   ram_write<=reset_n && read_beat && refill;
   ram_way<=way;ram_index<={set_index,beat};ram_payload<=rsp.r.data;
 end
 // No reset or asynchronous gate on BRAM read/write ports.
 always_ff @(posedge clk) begin
   rd0<=data0[read_index];rd1<=data1[read_index];
   if(ram_write) begin
     if(ram_way) data1[ram_index]<=ram_payload;
     else data0[ram_index]<=ram_payload;
   end
 end
 assign ready=reset_n && state==IDLE && !flush;
 assign response_valid=reset_n && state==DONE;
 always_comb begin
   req='0;
   req.ar.addr={32'b0,refill?{saved_address[31:5],5'b0}:saved_address};
   req.ar.len=refill?8'd7:8'd0;req.ar.size=2;req.ar.burst=1;
   req.ar.user=refill?4'hf:saved_mask;
   req.arvalid=reset_n && state==AR;req.rready=reset_n && state==R;
   req.aw.addr={32'b0,saved_address};req.aw.size=2;req.aw.burst=1;
   req.w.data=saved_write;req.w.strb=saved_mask;req.w.last=1;
   req.awvalid=reset_n && state==WRITE && !aw_done;
   req.wvalid=reset_n && state==WRITE && !w_done;
   req.bready=reset_n && state==B;
 end
 always_ff @(posedge clk) begin
   if(!reset_n) begin
     state<=IDLE;
     saved_address<=0;saved_write<=0;saved_mask<=0;cacheable<=0;refill<=0;
     way<=0;beat<=0;read_data<=0;fault<=0;refill_killed<=0;read_error<=0;aw_done<=0;w_done<=0;
   end else begin
     case(state)
       IDLE:if(valid && ready) begin
         saved_address<={address[31:2],2'b0};saved_write<=write_data;saved_mask<=mask;
         cacheable<=address>=32'h80000000 && address<32'hc0000000 &&
           !(address>=UNCACHED_BASE && address<UNCACHED_LIMIT) && address<32'hbf800000;
         refill<=0;beat<=0;fault<=0;refill_killed<=0;read_error<=0;aw_done<=0;w_done<=0;
         if(store) begin
           if(READ_ONLY) begin fault<=1;state<=DONE;end
           else begin
             state<=WRITE;
           end
         end else state<=LOOKUP;
       end
       LOOKUP:begin
         if(cacheable && (hit0 || hit1) && !inv_match && !flush) begin
           read_data<=hit0?rd0:rd1;state<=DONE;
         end else begin
           refill<=cacheable;way<=!valid0[set_index]?0:!valid1[set_index]?1:lru[set_index];
           refill_killed<=flush || inv_match;
           state<=AR;
         end
       end
       AR:if(rsp.arready)state<=R;
       R:if(rsp.rvalid) begin
         read_error<=read_error || rsp.r.resp[1] || rsp.r.last!=(refill?beat==7:1'b1);
         if(!refill || beat==saved_address[4:2])read_data<=rsp.r.data;
         if(!refill || beat==7) begin
           fault<=read_error || rsp.r.resp[1] || !rsp.r.last;
           if(refill && !read_error && !rsp.r.resp[1] && rsp.r.last && !refill_killed && !flush && !inv_match)begin
             if(way) tags1[set_index]<=tag;
             else tags0[set_index]<=tag;
           end
           state<=DONE;
         end else beat<=beat+1'b1;
       end
       WRITE:begin
         if(req.awvalid && rsp.awready)aw_done<=1;
         if(req.wvalid && rsp.wready)w_done<=1;
         if((aw_done || rsp.awready) && (w_done || rsp.wready))state<=B;
       end
       B:if(rsp.bvalid)begin fault<=rsp.b.resp[1];read_data<=0;state<=DONE;end
       DONE:if(response_ready)state<=IDLE;
       default:state<=IDLE;
     endcase
     if(state!=IDLE && (inv_match || flush))refill_killed<=1;
   end
 end
// Decode each per-set control once; avoid dynamic writes to a 512-bit vector.
 wire hit=cacheable && (hit0 || hit1) && !inv_match && !flush;
 wire victim=!valid0[set_index]?1'b0:!valid1[set_index]?1'b1:lru[set_index];
 wire allocate=state==R && rsp.rvalid && refill && beat==7 && !read_error && !rsp.r.resp[1] && rsp.r.last && !refill_killed && !flush && !inv_match;
 wire evict=state==LOOKUP && !hit && cacheable;
 wire store_accept=state==IDLE && valid && ready && store && !READ_ONLY;
 wire inv0=invalidate && tags0[invalidate_address[13:5]]==invalidate_address[31:14];
 wire inv1=invalidate && tags1[invalidate_address[13:5]]==invalidate_address[31:14];
 wire loc0=local_invalidate && tags0[local_address[13:5]]==local_address[31:14];
 wire loc1=local_invalidate && tags1[local_address[13:5]]==local_address[31:14];
 wire st0=store_accept && tags0[address[13:5]]==address[31:14];
 wire st1=store_accept && tags1[address[13:5]]==address[31:14];
 for(genvar n=0;n<512;n++)begin:metadata
   always_ff @(posedge clk)begin
     if(!reset_n || flush)begin valid0[n]<=0;valid1[n]<=0;end
     else begin
       if((inv0 && invalidate_address[13:5]==9'(n)) || (loc0 && local_address[13:5]==9'(n)) ||
          (st0 && address[13:5]==9'(n)) || (evict && !victim && set_index==9'(n)))valid0[n]<=0;
       else if(allocate && !way && set_index==9'(n))valid0[n]<=1;
       if((inv1 && invalidate_address[13:5]==9'(n)) || (loc1 && local_address[13:5]==9'(n)) ||
          (st1 && address[13:5]==9'(n)) || (evict && victim && set_index==9'(n)))valid1[n]<=0;
       else if(allocate && way && set_index==9'(n))valid1[n]<=1;
     end
     if(!reset_n)lru[n]<=0;
     else if(set_index==9'(n))begin
       if(state==LOOKUP && hit)lru[n]<=hit0;
       else if(allocate)lru[n]<=!way;
     end
   end
 end
`ifndef SYNTHESIS
 always @(posedge clk)if(reset_n && state==LOOKUP)
   assert(!(hit0 && hit1))else $fatal(1,"duplicate L1 physical tags");
`endif
endmodule
