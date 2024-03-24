module transfer_msr(
    input ref_clk,  //ICEStick 12MHz clock
    input rst,
    input data_req,
    output reg data_rdy,
    output reg [23:0] msr_data
);

    reg data_req_1;
    reg data_req_2;

    reg [23:0] timer_count;


    wire clk;

    //Generate 100MHz clock using PLL
    //Doesn't wotk with testbench, so needs to be commented when simulated
    //And clk should be connected to ref_clk as one of the options to simulate
    SB_PLL40_CORE #(
        .FEEDBACK_PATH("SIMPLE"),
        .PLLOUT_SELECT("GENCLK"),        
        .DIVR(4'b0000), // DIVR = 0
        .DIVF(7'b1000010), // DIVF = 66
        .DIVQ(3'b100), // DIVQ = 4
        .FILTER_RANGE(3'b001) // FILTER_RANGE = 1
    ) pll (
        .REFERENCECLK(ref_clk),
        .PLLOUTCORE(clk),
        .LOCK(),
        .RESETB(1'b1),
        .BYPASS(1'b0)
    );

    always @(posedge clk) begin
        if (rst) begin
            msr_data <= 24'b0;
            timer_count <= 24'b0;
            data_req_1 <= 1'b0;
            data_req_2 <= 1'b0;
        end else 
        begin
            // Since the data_req comes from the external source, we need 
            // to synchronize it See Harris, Harris, chapters 3.5.5, 4.4.4
            // or https://en.wikipedia.org/wiki/Incremental_encoder#Clock_synchronization
            if (data_req_1 & ~data_req_2) begin
                msr_data <= timer_count;
            end else if (data_req_1 & data_req_2) begin
                data_rdy <= 1'b1;
            end else if (~data_req_1) begin
                data_rdy <= 1'b0;
            end

            if (timer_count == 24'hFFFFFF) begin
                timer_count <= 24'b0;
            end else begin
                timer_count <= timer_count + 1;
            end

            data_req_1 <= data_req;
            data_req_2 <= data_req_1;

        end;
    end

endmodule

