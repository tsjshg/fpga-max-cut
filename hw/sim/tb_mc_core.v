`timescale 1ns / 1ps
// =====================================================================
//  mc_core の機能検証（xsim）。sim/run.sh <K> で走る
//
//  入力と期待値は sw/mkvec.py が CPU 版 sw/sa.c から作る（vec_k<K>/）。ジョブを何本か続けて流す。
//  出力の全部の語（レプリカごとの best・best_t・hit_t・最後の C、最後の塗り分け、
//  サイクル数・窓の数）と TLAST を照合する。実機では起きにくい状況をわざと作る:
//    - tvalid の欠落（入力が途切れる。ときどき長く止める）
//    - tready の背圧（出力が受け取らない）
// =====================================================================
`include "params.vh"
module tb;
  reg clk = 0, rstn = 0;
  always #4 clk = ~clk;                         // 125 MHz

  reg  [63:0] s_tdata;  reg s_tlast, s_tvalid;  wire s_tready;
  wire [63:0] m_tdata;  wire m_tlast, m_tvalid; reg  m_tready;
  wire [7:0]  m_tkeep;

  mc_core #(.K(`K_LANES), .KW(1)) dut (
    .aclk(clk), .aresetn(rstn),
    .s_axis_tdata(s_tdata), .s_axis_tkeep(8'hFF), .s_axis_tlast(s_tlast),
    .s_axis_tvalid(s_tvalid), .s_axis_tready(s_tready),
    .m_axis_tdata(m_tdata), .m_axis_tkeep(m_tkeep), .m_axis_tlast(m_tlast),
    .m_axis_tvalid(m_tvalid), .m_axis_tready(m_tready));

  reg [64:0] in_mem  [0:`NIN-1];
  reg [64:0] exp_mem [0:`NEXP-1];
  initial begin
    $readmemh(`IN_HEX, in_mem);
    $readmemh(`EXP_HEX, exp_mem);
  end

  integer sent = 0, got = 0, errors = 0, jobs_done = 0;

  // ---- 送り手（tvalid をときどき落とす。たまに長く止める） ----
  integer b;
  initial begin
    s_tvalid = 0; s_tlast = 0; s_tdata = 0;
    repeat (5) @(posedge clk);
    rstn = 1;
    for (b = 0; b < `NIN; b = b + 1) begin
      if ($urandom % 50 == 0) begin s_tvalid <= 0; repeat ($urandom % 40) @(posedge clk); end
      while ($urandom % 4 == 0) begin s_tvalid <= 0; @(posedge clk); end
      s_tdata  <= in_mem[b][63:0];
      s_tlast  <= in_mem[b][64];
      s_tvalid <= 1;
      @(posedge clk);
      while (!s_tready) @(posedge clk);
      sent = sent + 1;
    end
    s_tvalid <= 0;
  end

  // ---- 受け手（tready をときどき落とす） ----
  always @(posedge clk) m_tready <= ($urandom % 3 != 0);

  integer job_beat = 0;
  always @(posedge clk) if (rstn && m_tvalid && m_tready) begin
    if (got >= `NEXP) begin
      errors = errors + 1;
      $display("MISMATCH: extra beat %h", m_tdata);
    end else begin
      if (job_beat == 1)
        $display("job %0d: run cycles %0d (expected %0d)", jobs_done, m_tdata, exp_mem[got][63:0]);
      if ({m_tlast, m_tdata} !== exp_mem[got] || m_tkeep !== 8'hFF) begin
        errors = errors + 1;
        if (errors <= 20)
          $display("MISMATCH job %0d beat %0d: got %b_%h, expected %b_%h", jobs_done, job_beat,
                   m_tlast, m_tdata, exp_mem[got][64], exp_mem[got][63:0]);
      end
    end
    got = got + 1;
    job_beat = job_beat + 1;
    if (m_tlast) begin jobs_done = jobs_done + 1; job_beat = 0; end
  end

  initial begin
    #40000000;
    $display("TIMEOUT: sent %0d of %0d, received %0d of %0d beats", sent, `NIN, got, `NEXP);
    $finish;
  end
  always @(posedge clk) if (got == `NEXP) begin
    repeat (100) @(posedge clk);   // 余計な語が出てこないことも見る
    if (errors == 0) $display("PASS: K=%0d, %0d jobs, %0d output beats, all match", `K_LANES, `NJOBS, got);
    else             $display("FAIL: %0d of %0d beats differ", errors, got);
    $finish;
  end
endmodule
