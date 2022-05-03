`timescale 1ns / 1ps

module pipe_stage #(
    parameter WIDTH=8
) (
    input i_clk,
    input i_reset_n,

    input [WIDTH-1:0] i_data,
    input i_vld,
    output o_rdy,

    output [WIDTH-1:0] o_data,
    output o_vld,
    input i_rdy
);

    initial $monitor("Somebody changed my clk to %d\n", i_clk);
    
endmodule


module tb;

pipe_stage DUT();

event my_ev;

always @(my_ev) begin
    $display("Someone triggered an event!\n");
end

initial begin
    $display("Begin typing lua code. (Sorry, no autocomplete!)");
    $lua_repl;
    #10
    $display("Goodbye");
    $finish;
end

endmodule