`timescale 1ns / 1ps

module pipe_stage #(
    parameter WIDTH=8
) (
    input clk,
    input i_reset_n,

    input [WIDTH-1:0] i_data,
    input i_vld,
    output o_rdy,

    output [WIDTH-1:0] o_data,
    output o_vld,
    input i_rdy
);

    //initial $monitor("Somebody changed my clk to %d", clk);

    wire rd_sig = o_vld && i_rdy;
    wire wr_sig = i_vld && o_rdy;
    
    reg [1:0][WIDTH-1:0] storage;
    reg [1:0]            storage_vld;
    wire [1:0]           storage_vld_nxt;
    
    
    generate 
    for (genvar i = 0; i < 2; i = i + 1) begin
        always @(posedge clk) begin
            if (!i_reset_n) begin
                storage[i] <= {WIDTH{1'b0}};
            end else if (wr_sig && (wr_ptr == i[0])) begin
                storage[i] <= i_data;
            end
        end

        always @(posedge clk) begin
            if (!i_reset_n || (rd_sig && (rd_ptr==i[0]))) begin
                storage_vld[i] <= 1'b0;
            end else if (wr_sig && wr_ptr==i[0]) begin
                storage_vld[i] <= 1'b1;
            end
        end

        // TODO: We could put some kind of verilog behavioural
        // assert here that we don't read and write to the same
        // slot, but we will do it in Lua instead.
        // - IMO it's better to avoid putting sim-only code in
        //   your RTL
        // - Ultimately I'm trying to test the Lua interface
    end 
    endgenerate

    reg rd_ptr; wire rd_ptr_nxt;
    reg wr_ptr; wire wr_ptr_nxt;

    always @(posedge clk) begin
        if (!i_reset_n) begin
            rd_ptr <= 0;
            wr_ptr <= 0;
        end else begin
            rd_ptr <= rd_ptr_nxt;
            wr_ptr <= wr_ptr_nxt;
        end
    end

    assign rd_ptr_nxt = rd_ptr ^ rd_sig;
    assign wr_ptr_nxt = wr_ptr ^ wr_sig;
    
    assign o_data = storage[rd_ptr];
    assign o_vld = storage_vld[rd_ptr];
    assign o_rdy = !storage_vld[wr_ptr];
endmodule

module counter # (
    parameter WIDTH=8
) (
    input clk,
    input i_reset_n,

    input i_en,
    output [WIDTH-1:0] o_cnt
);
    reg [WIDTH-1:0] cnt;

    always @(posedge clk) begin
        if (!i_reset_n) begin
            cnt <= {WIDTH{1'b0}};
        end else begin
            cnt <= cnt + 1'b1; //Should be unsigned... right?
                               //Gotta check the gotchas...
        end
    end

    assign o_cnt = cnt;
endmodule



module tb;

    //Simple thing to test value-change callbacks
    reg thing = 0;
    always #2 thing = ~thing;
    
    pipe_stage DUT();


    //Simple thing to test those tricky time-related callbacks
    //for the different phases of the verilog event queue
    
    //Hmm... it might be a bad sign that it's still more
    //convenient to make certain testbench inputs in verilog...
    //Maybe if I had a better support library it would be
    //nicer in lua?
    reg tb_clk = 0;
    always #5 tb_clk = ~tb_clk;
    reg tb_rstn = 0;
    initial #10 tb_rstn = 1;
    wire [7:0] cnt;
    counter u_cnt(
        .clk(tb_clk),
        .i_reset_n(tb_rstn),
        .i_en(1'b1),
        .o_cnt(cnt)
    );

    //Simple thing to test events
    event my_ev; 
    always @(my_ev) begin
        $display("Someone triggered an event!\n");
    end
    
    initial begin
        // "dump" is such an ugly word :-/
        $dumpfile("sim_out.vcd");
        $dumpvars;
        $dumpon;
        $dumplimit(10240);
        $display("Begin typing lua code. (Sorry, no autocomplete!)");
        $lua_repl;
        #40
        $display("Goodbye");
        $finish;
    end

endmodule