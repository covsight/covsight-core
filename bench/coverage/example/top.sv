// Small synthesizable design used to generate REAL Verilator code coverage
// (line + toggle) for the coverage→NCDB benchmark pipeline. Wide datapaths and
// several always blocks give a non-trivial number of toggle/line points.

module alu #(parameter W = 32) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic [2:0]       op,
    input  logic [W-1:0]     a,
    input  logic [W-1:0]     b,
    output logic [W-1:0]     result,
    output logic             zero
);
    logic [W-1:0] r;
    always_comb begin
        case (op)
            3'd0: r = a + b;
            3'd1: r = a - b;
            3'd2: r = a & b;
            3'd3: r = a | b;
            3'd4: r = a ^ b;
            3'd5: r = a << b[4:0];
            3'd6: r = a >> b[4:0];
            default: r = '0;
        endcase
    end
    assign result = r;
    assign zero   = (r == '0);
endmodule

module regfile #(parameter W = 32, parameter N = 16) (
    input  logic             clk,
    input  logic             we,
    input  logic [3:0]       waddr,
    input  logic [W-1:0]     wdata,
    input  logic [3:0]       raddr,
    output logic [W-1:0]     rdata
);
    logic [W-1:0] mem [N];
    always_ff @(posedge clk) begin
        if (we)
            mem[waddr] <= wdata;
    end
    assign rdata = mem[raddr];
endmodule

module top #(parameter W = 32) (
    input  logic         clk,
    input  logic         rst_n,
    input  logic [7:0]   stim,
    output logic [W-1:0] out
);
    logic [2:0]   op;
    logic [W-1:0] a, b, alu_out, rf_out;
    logic         alu_zero, we;
    logic [3:0]   waddr, raddr;
    logic [W-1:0] acc;

    assign op    = stim[2:0];
    assign waddr = stim[7:4];
    assign raddr = stim[3:0];
    assign we    = stim[3];

    alu     #(.W(W)) u_alu (.clk, .rst_n, .op, .a, .b, .result(alu_out), .zero(alu_zero));
    regfile #(.W(W)) u_rf  (.clk, .we, .waddr, .wdata(alu_out), .raddr, .rdata(rf_out));

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            a   <= '0;
            b   <= '0;
            acc <= '0;
        end else begin
            a <= {a[W-9:0], stim};
            b <= rf_out;
            if (alu_zero)
                acc <= acc + 1;
            else if (alu_out[0])
                acc <= acc ^ alu_out;
            else
                acc <= acc + alu_out;
        end
    end

    assign out = acc;
endmodule
