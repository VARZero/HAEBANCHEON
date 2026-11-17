// Parameterized level-triggered sources, standard PLIC M/S contexts for hart 0.
// A claimed source cannot be re-pended until its owning context completes it.
module or_storage_plic #(parameter int SOURCES=2, INDEX_BITS=$clog2(SOURCES), ID_BITS=$clog2(SOURCES+1))(
 input wire clk,reset_n,input wire [SOURCES-1:0] sources,
 input or_bo_axi_pkg::al_req_t req,output or_bo_axi_pkg::al_rsp_t rsp,
 output wire m_irq,s_irq
);
 wire valid,store;wire [31:0] address,write_data;wire [3:0] mask;
 logic [31:0] read_data;wire error=address[1:0]!=0 || (store && mask!=15);
 gen3_axil_regs bus(.clk,.reset_n,.req,.rsp,.valid,.store,.address,.write_data,.mask,
  .ready(1'b1),.error,.read_data);
 logic [2:0] priority_value[SOURCES],threshold[2];
 logic [SOURCES-1:0] enabled[2],pending,in_service,service_owner;
 logic [ID_BITS-1:0] selected[2];
 wire [21:0] offset=address[21:0];
 assign m_irq=selected[0]!=0;assign s_irq=selected[1]!=0;
 always_comb begin
   for(int c=0;c<2;c++) begin
     selected[c]=0;
     for(int i=SOURCES-1;i>=0;i--) if(pending[i] && enabled[c][i] && priority_value[i]>threshold[c] &&
       (selected[c]==0 || priority_value[i]>=priority_value[INDEX_BITS'(selected[c]-1'b1)])) selected[c]=ID_BITS'(i+1);
   end
   read_data=0;
   case(offset)
    'h1000:read_data=32'(pending)<<1;
    'h2000:read_data=32'(enabled[0])<<1;
    'h2080:read_data=32'(enabled[1])<<1;
    'h200000:read_data={29'b0,threshold[0]};
    'h201000:read_data={29'b0,threshold[1]};
    'h200004:read_data=32'(selected[0]);
    'h201004:read_data=32'(selected[1]);
    default:;
   endcase
   for(int i=0;i<SOURCES;i++)if(offset==22'(4*(i+1)))read_data={29'b0,priority_value[i]};
 end
 always_ff @(posedge clk) begin
   if(!reset_n) begin
     pending<=0;in_service<=0;service_owner<=0;
     for(int i=0;i<SOURCES;i++)priority_value[i]<=0;
     for(int c=0;c<2;c++)begin threshold[c]<=0;enabled[c]<=0;end
   end else begin
     pending<=pending | (sources & ~in_service);
     if(valid && !error) begin
       if(store) case(offset)
        'h2000:enabled[0]<=write_data[SOURCES:1];
        'h2080:enabled[1]<=write_data[SOURCES:1];
        'h200000:threshold[0]<=write_data[2:0];
        'h201000:threshold[1]<=write_data[2:0];
        default:;
       endcase
       if(store)for(int i=0;i<SOURCES;i++)if(offset==22'(4*(i+1)))priority_value[i]<=write_data[2:0];
       for(int c=0;c<2;c++) if(offset==(c==0?22'h200004:22'h201004)) begin
         if(!store && selected[c]!=0) begin
           pending[INDEX_BITS'(selected[c]-1'b1)]<=0;in_service[INDEX_BITS'(selected[c]-1'b1)]<=1;service_owner[INDEX_BITS'(selected[c]-1'b1)]<=1'(c);
         end else if(store && write_data>=1 && write_data<=SOURCES &&
           in_service[INDEX_BITS'(write_data-1'b1)] && service_owner[INDEX_BITS'(write_data-1'b1)]==1'(c)) in_service[INDEX_BITS'(write_data-1'b1)]<=0;
       end
     end
   end
 end
endmodule
