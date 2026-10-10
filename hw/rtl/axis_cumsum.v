`timescale 1ns / 1ps
// =====================================================================
//  ひな形の回路: AXI4-Stream の累積和
//
//  64bit のビートを 32bit × 2 レーンとみなし、レーンごとに累積和を出す。
//  TLAST のビートで和を 0 に戻すので、DMA の1転送が1本の独立した列になる。
//    out[i] = in[0] + in[1] + … + in[i]   （レーンごと・32bit で桁あふれは捨てる）
//
//  ここを自分の回路に書き換える。残しておくべき作法は3つ:
//  1. 出力レジスタは「空いているか、下流が受け取ったとき」だけ載せ替える
//     （load = !m_tvalid || m_tready）。無条件に載せ替えると背圧でビートを落とす
//  2. 入力の tready は load をそのまま返す。段を増やしたら、データと有効ビットを
//     同じ段数だけ遅らせること（ずれると「合計は合うが個々の値が1つずれる」）
//  3. TLAST は必ず出口まで運ぶ。届かないと DMA の S2MM が永久に終わらない
//
//  X_INTERFACE_INFO を明示しているのは、名前からの推論まかせだと IPI が
//  ストリームのインタフェースとして束ねてくれないことがあるため。
// =====================================================================
module axis_cumsum (
  (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
  (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis:m_axis, ASSOCIATED_RESET aresetn" *)
  input  wire        aclk,
  (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
  (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
  input  wire        aresetn,

  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TDATA" *)
  input  wire [63:0] s_axis_tdata,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TKEEP" *)
  input  wire [7:0]  s_axis_tkeep,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TLAST" *)
  input  wire        s_axis_tlast,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TVALID" *)
  input  wire        s_axis_tvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis TREADY" *)
  output wire        s_axis_tready,

  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
  output reg  [63:0] m_axis_tdata,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TKEEP" *)
  output wire [7:0]  m_axis_tkeep,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TLAST" *)
  output reg         m_axis_tlast,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
  output reg         m_axis_tvalid,
  (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
  input  wire        m_axis_tready
);

  reg [31:0] acc0, acc1;                       // レーンごとの「ここまでの和」
  wire [31:0] sum0 = acc0 + s_axis_tdata[31:0];
  wire [31:0] sum1 = acc1 + s_axis_tdata[63:32];

  wire load = !m_axis_tvalid || m_axis_tready; // 出力の席が空く
  assign s_axis_tready = load;
  assign m_axis_tkeep  = 8'hFF;                // 常に 8 バイト全部が有効

  always @(posedge aclk) begin
    if (!aresetn) begin
      m_axis_tvalid <= 1'b0;
      acc0 <= 32'd0;
      acc1 <= 32'd0;
    end else if (load) begin
      m_axis_tvalid <= s_axis_tvalid;
      if (s_axis_tvalid) begin
        m_axis_tdata <= {sum1, sum0};
        m_axis_tlast <= s_axis_tlast;
        acc0 <= s_axis_tlast ? 32'd0 : sum0;
        acc1 <= s_axis_tlast ? 32'd0 : sum1;
      end
    end
  end

endmodule
