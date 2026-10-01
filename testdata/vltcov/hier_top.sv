module sub #(parameter int W=2) (input logic clk, input logic [W-1:0] d);
  logic [W-1:0] q;
  typedef enum logic [1:0] { S0, S1, S2 } st_e;
  st_e st = S0;
  always @(posedge clk) begin
    q <= d;
    case (st)
      S0: st <= S1;
      S1: if (d[0]) st <= S2; else st <= S0;
      S2: st <= S0;
      default: st <= S0;
    endcase
  end
  covergroup sub_cg @(posedge clk);
    cp_d : coverpoint d;
    cp_q : coverpoint q { bins zero = {0}; bins other = default; }
    x_dq : cross cp_d, cp_q;
  endgroup
  sub_cg cg_i = new;
endmodule

module hier_top;
  logic clk = 0;
  logic [1:0] cnt = 0;
  always #5 clk = ~clk;
  always @(posedge clk) cnt <= cnt + 1;
  sub u_a(.clk(clk), .d(cnt));
  sub u_b(.clk(clk), .d(~cnt));
  initial begin #60; $finish; end
endmodule
