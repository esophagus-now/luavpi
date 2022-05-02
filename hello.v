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

    
endmodule


module tb;

pipe_stage test();
    
initial begin
    $display("Begin typing lua code. (Sorry, no autocomplete!)");
    $hello;
    #10
    $display("Goodbye");
    $finish;
end

endmodule