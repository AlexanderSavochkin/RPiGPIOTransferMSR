`timescale 100ns/10ps

module transfer_msr_tb;


//100 MHz clock
reg clk = 0;
reg rst = 1;
reg data_req;

wire data_rdy;
wire [23:0] data_out;

always begin
    #1 
    clk = ~clk;
end

transfer_msr transfer_msr_inst(
    .ref_clk(clk),
    .rst(rst),
    .data_req(data_req),
    .data_rdy(data_rdy),
    .msr_data(data_out)
);

initial begin
    #10
    rst = 0;
    #10
    data_req = 1;
    #4
    data_req = 0;
    #10
    data_req = 1;
    #4
    data_req = 0;
    #20
    data_req = 1;
    #4
    data_req = 0;
    #50;
end

initial begin
    $dumpfile("transfer_msr_tb.vcd");
    $dumpvars(0, transfer_msr_tb);

    #150

    $display("Starting finished");
    $finish;
end

endmodule

