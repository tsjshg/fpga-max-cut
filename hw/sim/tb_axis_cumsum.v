`timescale 1ns / 1ps
// =====================================================================
//  axis_cumsum の機能検証（xsim）。./run.sh で走る
//
//  実機へ持って行く前に、論理の誤りはここで潰す。実機で確かめるのは
//  タイミングと帯域だけにする。そのために、実機では起きにくい状況をわざと作る:
//    - tvalid の欠落（入力が途切れる）
//    - tready の背圧（出力が受け取らない）
//    - 長さの違う列を続けて流す（長さ 1 の列も含む）
//  期待値はテストベンチの中で、回路とは別の書き方（列ごとの単純な足し算）で作る。
// =====================================================================
module tb;
  reg clk = 0, rstn = 0;
  always #4 clk = ~clk;                         // 125 MHz

  reg  [63:0] s_tdata;  reg s_tlast, s_tvalid;  wire s_tready;
  wire [63:0] m_tdata;  wire m_tlast, m_tvalid; reg  m_tready;
  wire [7:0]  m_tkeep;

  axis_cumsum dut (
    .aclk(clk), .aresetn(rstn),
    .s_axis_tdata(s_tdata), .s_axis_tkeep(8'hFF), .s_axis_tlast(s_tlast),
    .s_axis_tvalid(s_tvalid), .s_axis_tready(s_tready),
    .m_axis_tdata(m_tdata), .m_axis_tkeep(m_tkeep), .m_axis_tlast(m_tlast),
    .m_axis_tvalid(m_tvalid), .m_axis_tready(m_tready));

  localparam NPKT = 6;
  integer len [0:NPKT-1];
  initial begin len[0]=1; len[1]=2; len[2]=17; len[3]=1; len[4]=64; len[5]=5; end

  // 期待値: 送り手と同じ乱数列を受け手側でもう一度引いて、自分で足す
  integer seed_tx = 7, seed_rx = 7;
  integer sent = 0, got = 0, errors = 0, total = 0;

  // ---- 送り手（tvalid をときどき落とす） ----
  integer p, b;
  reg [31:0] t0, t1;
  initial begin
    s_tvalid = 0; s_tlast = 0; s_tdata = 0;
    for (p = 0; p < NPKT; p = p + 1) total = total + len[p];
    repeat (5) @(posedge clk);
    rstn = 1;
    for (p = 0; p < NPKT; p = p + 1)
      for (b = 0; b < len[p]; b = b + 1) begin
        while ($urandom % 4 == 0) begin s_tvalid <= 0; @(posedge clk); end
        t1 = $random(seed_tx); t0 = $random(seed_tx);   // 引く順を決めておく（{…, …} の評価順は処理系次第）
        s_tdata  <= {t1, t0};
        s_tlast  <= (b == len[p] - 1);
        s_tvalid <= 1;
        @(posedge clk);
        while (!s_tready) @(posedge clk);
        sent = sent + 1;
      end
    s_tvalid <= 0;
  end

  // ---- 受け手（tready をときどき落とす） ----
  always @(posedge clk) m_tready <= ($urandom % 3 != 0);

  integer rp = 0, rb = 0;
  reg [31:0] e0 = 0, e1 = 0, x0, x1;
  always @(posedge clk) if (rstn && m_tvalid && m_tready) begin
    x1 = $random(seed_rx); x0 = $random(seed_rx);   // 送り手と同じ順に引く
    e0 = e0 + x0;  e1 = e1 + x1;
    if (m_tdata !== {e1, e0} || m_tlast !== (rb == len[rp] - 1) || m_tkeep !== 8'hFF) begin
      errors = errors + 1;
      if (errors <= 10)
        $display("MISMATCH packet %0d beat %0d: got %h last=%b, expected %h last=%b",
                 rp, rb, m_tdata, m_tlast, {e1, e0}, rb == len[rp] - 1);
    end
    got = got + 1;
    if (rb == len[rp] - 1) begin rp = rp + 1; rb = 0; e0 = 0; e1 = 0; end
    else rb = rb + 1;
  end

  initial begin
    #200000;
    $display("TIMEOUT: sent %0d, received %0d of %0d beats", sent, got, total);
    $finish;
  end
  always @(posedge clk) if (got == total && total > 0) begin
    if (errors == 0) $display("PASS: %0d beats in %0d packets, all match", total, NPKT);
    else             $display("FAIL: %0d of %0d beats differ", errors, total);
    $finish;
  end
endmodule
