`timescale 1ns / 1ps
// =====================================================================
//  mc_core: 重みなし Max-Cut の焼きなましエンジン（AXI4-Stream 1 本入れて 1 本返す）
//
//  仕様は docs/m1-design.md。規則は CPU 版 sw/sa.c とビット単位で同じ。
//    1. 入力ストリームからジョブ（グラフ・初期の塗り分け・種・温度の表）を読み込む
//    2. K 本のレーン × P 個のレプリカで焼きなましを回す（外とのやりとりは無い）
//    3. 結果（レプリカごとの best・best_t・hit_t・最後の C と、最後の塗り分け）を返す
//
//  塗り分けは「頂点ごとに全レプリカぶんの W = K·P bit の語」で BRAM に持つ。全レプリカが
//  同じ頂点を P クロックかけて処理し（窓）、その間に次の窓の隣の語を先読みする。
//  時刻表と、それで正しくなる理由は docs/m1-design.md の「窓」の節。
// =====================================================================
module mc_core #(
  parameter P     = 16,
  parameter JW    = 4,      // log2(P)
  parameter K     = 1,      // レーンの数
  parameter KW    = 1,      // max(1, log2(K))
  parameter D     = 4,
  parameter NMAX  = 4096,
  parameter NW    = 12,     // log2(NMAX)
  parameter SMAX  = 256,    // 温度の段数の上限
  parameter SW    = 8       // log2(SMAX)
) (
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

  localparam W  = K * P;    // 塗り分けの語の幅
  localparam R  = K * P;    // レプリカの数
  localparam CB = (W + 63) / 64;   // 塗り分けの語1つを送るビート数（ビート b = ビット 64b..64b+63）
  localparam WC = CB * 64;
  localparam CBW = (CB > 1) ? $clog2(CB) : 1;

  // 窓の中の先読みの時刻（docs/m1-design.md の時刻表）。
  // BRAM は「時刻 o に読み番地のレジスタへ載せる → o+1 で読む → o+2 で rd1 → o+3 で rd2 が見える」。
  // 書き込みも同じく、o に we/番地/値のレジスタへ載せ、o+1 の終わりに書かれる。
  localparam O_ADJ  = 0;    // ADJ の番地を載せる
  localparam O_NBI  = 3;    // 隣の番号を受け取る（adj_rd2）
  localparam O_WB   = 3;    // 1つ前の窓の頂点を書き戻す（o=4 の終わりに書かれる）
  localparam O_RD0  = 4;    // 隣 0..D-1 の番地を載せる（4..7、o=5.. で読む）、続いて次の頂点（8）
  localparam O_GET0 = 7;    // 読んだ語を受け取る（7..10、11）
  localparam [7:0] P8 = P;
  localparam [15:0] K16 = K;

  // ---------------- 状態 ----------------
  localparam ST_LOAD = 3'd0, ST_PRE = 3'd1, ST_RUN = 3'd2, ST_POST = 3'd3, ST_OUT = 3'd4;
  reg [2:0] state;

  localparam L_HDR0 = 3'd0, L_HDR1 = 3'd1, L_ADJ = 3'd2, L_COL = 3'd3,
             L_SEED = 3'd4, L_C0 = 3'd5, L_SCHED = 3'd6;
  reg [2:0]  lsec;
  reg [15:0] lcnt;

  reg [15:0] n_reg;
  reg [7:0]  d_in;
  reg [15:0] s_reg;
  reg [15:0] target;

  assign s_axis_tready = (state == ST_LOAD);
  wire beat = s_axis_tvalid && s_axis_tready;
  wire [63:0] din = s_axis_tdata;

  // ---------------- メモリ ----------------
  (* ram_style = "block" *) reg [W-1:0]    col_mem [0:NMAX-1];
  (* ram_style = "block" *) reg [4*NW-1:0] adj_mem [0:NMAX-1];
  // 温度の表。書き口 1・読み口 1（番地はレジスタ）にして、LUTRAM に載るようにする。
  // 読み番地は「次に切り替える段」。段は n 窓以上続くので、切り替えの時には読み終わっている。
  (* ram_style = "distributed" *) reg [31:0] sw_mem [0:SMAX-1];
  (* ram_style = "distributed" *) reg [63:0] th_mem [0:SMAX-1];
  wire          sch_we = beat && (lsec == L_SCHED);
  wire [SW-1:0] sch_wa = lcnt[SW:1];
  reg  [SW-1:0] sch_ra;
  reg  [31:0]   sch_sw;
  reg  [63:0]   sch_th;
  always @(posedge aclk) begin
    if (sch_we && !lcnt[0]) sw_mem[sch_wa] <= din[31:0];
    if (sch_we &&  lcnt[0]) th_mem[sch_wa] <= din;
    sch_sw <= sw_mem[sch_ra];
    sch_th <= th_mem[sch_ra];
  end

  reg          col_we;
  reg [NW-1:0] col_wa;
  reg [W-1:0]  col_wd;
  reg [NW-1:0] col_ra;
  // 読み込みで塗り分けの語を組み立てる（ビート 0 が下位）
  reg [CBW-1:0] lcs;
  reg [WC-1:0]  colacc;
  wire [WC-1:0] colnext;
  generate
    if (CB == 1) begin : cn1
      assign colnext = din;
    end else begin : cnn
      assign colnext = {din, colacc[WC-1:64]};
    end
  endgenerate
  reg [W-1:0]  col_rd1, col_rd2;
  always @(posedge aclk) begin
    if (col_we) col_mem[col_wa] <= col_wd;
    col_rd1 <= col_mem[col_ra];
    col_rd2 <= col_rd1;
  end

  reg          adj_we;
  reg [NW-1:0] adj_wa;
  reg [4*NW-1:0] adj_wd;
  reg [NW-1:0] adj_ra;
  reg [4*NW-1:0] adj_rd1, adj_rd2;
  always @(posedge aclk) begin
    if (adj_we) adj_mem[adj_wa] <= adj_wd;
    adj_rd1 <= adj_mem[adj_ra];
    adj_rd2 <= adj_rd1;
  end

  // ---------------- 窓の制御 ----------------
  (* max_fanout = 32 *)
  reg [JW-1:0] o;                 // 窓の中の時刻 = 今 S0 に入るスロット（全レーンの選択に配る）
  reg [NW-1:0] vprev, vcur, vnext;
  reg          cur_valid, prev_valid;
  reg [31:0]   t_cur;              // 窓の番号。1 ジョブの窓の数は 2^32 − 1 未満（host 側で確かめる）
  reg          curbuf;
  reg [W-1:0]  own0, own1;
  reg [W-1:0]  nb   [0:D-1];
  reg [W-1:0]  nbn  [0:D-1];
  reg [D-1:0]  fwd, fwdn;
  reg [W-1:0]  ownn;
  reg [NW-1:0] nbidx [0:D-1];
  reg [SW-1:0] stage;
  reg [31:0]   sweep_cnt, cur_sweeps;
  reg [31:0]   th0, th1;
  reg [63:0]   cycles;

  wire [W-1:0] own_prev = curbuf ? own0 : own1;   // 書き戻し用（o = O_WB でだけ使う）
  wire win_end = (o == P - 1);
  wire in_win  = (state == ST_PRE) || (state == ST_RUN) || (state == ST_POST);

  // ---------------- レーン ----------------
  wire [K-1:0]  s3_flip;
  wire [K-1:0]  s3_buf;
  wire [JW-1:0] s3_j [0:K-1];
  wire [15:0]   rd_best [0:K-1];
  wire [15:0]   rd_c    [0:K-1];
  wire [31:0]   rd_bt   [0:K-1];
  wire [31:0]   rd_ht   [0:K-1];
  wire [K-1:0]  rd_btn, rd_htn;
  reg  [JW-1:0] rd_j;
  reg  [KW-1:0] rd_k;

  wire [KW-1:0] ld_k = lcnt[JW +: KW];
  wire [JW-1:0] ld_j = lcnt[JW-1:0];

  genvar gk, gi;
  generate
    for (gk = 0; gk < K; gk = gk + 1) begin : lane
      // レーン k のスロット o のビットだけを先に選び、それから OWN の 0/1 を選ぶ
      // （W bit の語ごと選んでからビットを選ぶと、curbuf が W 個の選択に広がるため）
      wire own0b = own0[gk*P + o];
      wire own1b = own1[gk*P + o];
      wire ownc  = curbuf ? own1b : own0b;
      wire ownp  = curbuf ? own0b : own1b;
      wire [D-1:0] nbbits;
      for (gi = 0; gi < D; gi = gi + 1) begin : nbsel
        assign nbbits[gi] = fwd[gi] ? ownp : nb[gi][gk*P + o];
      end
      wire kmatch = (K == 1) || (ld_k == gk);
      mc_lane #(.P(P), .JW(JW), .D(D)) u (
        .clk(aclk),
        .s0_valid(state == ST_RUN), .s0_j(o), .s0_t(t_cur), .s0_buf(curbuf),
        .s0_nb(nbbits), .s0_own(ownc),
        .s0_th0(th0), .s0_th1(th1), .target(target),
        .s3_flip(s3_flip[gk]), .s3_buf_o(s3_buf[gk]), .s3_j_o(s3_j[gk]),
        .ld_seed_we_i(beat && lsec == L_SEED && kmatch),
        .ld_c0_we_i(beat && lsec == L_C0 && kmatch),
        .ld_j_i(ld_j), .ld_data_i(din),
        .rd_en_i(state == ST_OUT), .rd_j(rd_j), .rd_best(rd_best[gk]), .rd_c(rd_c[gk]),
        .rd_best_t(rd_bt[gk]), .rd_hit_t(rd_ht[gk]),
        .rd_bt_none(rd_btn[gk]), .rd_ht_none(rd_htn[gk]));
    end
  endgenerate

  // ---------------- 出力（生成 → 2 段 → FIFO → 出力レジスタ） ----------------
  localparam G_HDR = 2'd0, G_REP = 2'd1, G_COL = 2'd2, G_DONE = 2'd3;
  reg [1:0]  gsec;
  reg [15:0] gcnt;
  reg [1:0]  gsub;
  reg [CBW-1:0] gcs;               // 塗り分けの語の中のビート番号
  reg        g1_v, g1_col, g1_last;  reg [63:0] g1_d;  reg [CBW-1:0] g1_cs;
  reg        g1_rep;  reg [1:0] g1_sub;
  reg        g2_rep;  reg [1:0] g2_sub;  reg [KW-1:0] rd_k2;
  reg        g2_v, g2_col, g2_last;  reg [63:0] g2_d;  reg [CBW-1:0] g2_cs;
  reg        g3_v, g3_col, g3_last;  reg [63:0] g3_d;  reg [CBW-1:0] g3_cs;
  wire [WC-1:0] col_rd2p = col_rd2;

  localparam FD = 16;
  reg [64:0] fifo [0:FD-1];
  reg [3:0]  f_wp, f_rp;
  reg [4:0]  f_cnt;
  wire f_push = g3_v;
  wire o_load = !m_axis_tvalid || m_axis_tready;
  wire f_pop  = o_load && (f_cnt != 0);
  wire g_can  = (state == ST_OUT) && (gsec != G_DONE) && (f_cnt + g1_v + g2_v + g3_v < FD - 1);
  assign m_axis_tkeep = 8'hFF;

  // 生成段で出す語。見出しは生成段で作る。レプリカの語は、生成段で番地 rd_j・rd_k をレジスタに載せ、
  // g1 の段で各レーンが LUTRAM を読んでレーンの中のレジスタに受け、g2 の段で 48 本から選んで作る
  // （K = 48 で「状態 → 番地の選択 → LUTRAM → 48 本の選択」が 1 段に並んで WNS −2.7 ns になった）。
  // 塗り分けは BRAM で、生成段で番地を載せると3段後の g3 と同じ時刻に col_rd2 に出る。
  reg [63:0] g_word;
  reg        g_last;
  always @(*) begin
    g_word = 64'd0;
    g_last = 1'b0;
    case (gsec)
      G_HDR: case (gsub)
        2'd0: g_word = {16'h4D43, s_reg, P8, d_in, n_reg};
        2'd1: g_word = cycles;
        default: g_word = {K16, 16'd0, t_cur};
      endcase
      G_REP: ;
      default: g_last = (gcnt == n_reg - 1) && (gcs == CB - 1);
    endcase
  end
  reg [63:0] rep_word;   // g2 の段で、レーンが受けた語から作る
  always @(*) begin
    case (g2_sub)
      2'd0: rep_word = {32'd0, rd_c[rd_k2], rd_best[rd_k2]};
      // 全 1（無し）は 48 bit の全 1 に広げる（出力の形は変えない）
      2'd1: rep_word = {16'd0, {16{rd_btn[rd_k2]}}, rd_bt[rd_k2]};
      default: rep_word = {16'd0, {16{rd_htn[rd_k2]}}, rd_ht[rd_k2]};
    endcase
  end

  // ---------------- 本体 ----------------
  integer i, kk;
  always @(posedge aclk) begin
    col_we <= 1'b0;
    adj_we <= 1'b0;
    if (!aresetn) begin
      state <= ST_LOAD;
      lsec  <= L_HDR0;
      lcnt  <= 16'd0;
      m_axis_tvalid <= 1'b0;
      f_wp <= 0; f_rp <= 0; f_cnt <= 0;
      g1_v <= 1'b0; g2_v <= 1'b0; g3_v <= 1'b0;
    end else begin
      // ======== 読み込み ========
      if (beat) begin
        case (lsec)
          L_HDR0: begin
            n_reg <= din[15:0];
            d_in  <= din[23:16];
            s_reg <= din[47:32];
            lsec  <= L_HDR1;
          end
          L_HDR1: begin
            target <= din[15:0];
            lcs <= 0;
            lsec <= L_ADJ;
            lcnt <= 16'd0;
          end
          L_ADJ: begin
            adj_we <= 1'b1;
            adj_wa <= lcnt[NW-1:0];
            adj_wd <= {din[48 +: NW], din[32 +: NW], din[16 +: NW], din[0 +: NW]};
            lcnt <= (lcnt == n_reg - 1) ? 16'd0 : lcnt + 1;
            if (lcnt == n_reg - 1) lsec <= L_COL;
          end
          L_COL: begin  // 頂点ごとに CB ビート。そろったら書く
            colacc <= colnext;
            lcs <= (lcs == CB - 1) ? 0 : lcs + 1;
            if (lcs == CB - 1) begin
              col_we <= 1'b1;
              col_wa <= lcnt[NW-1:0];
              col_wd <= colnext[W-1:0];
              lcnt <= (lcnt == n_reg - 1) ? 16'd0 : lcnt + 1;
              if (lcnt == n_reg - 1) lsec <= L_SEED;
            end
          end
          L_SEED: begin
            lcnt <= (lcnt == R - 1) ? 16'd0 : lcnt + 1;
            if (lcnt == R - 1) lsec <= L_C0;
          end
          L_C0: begin
            lcnt <= (lcnt == R - 1) ? 16'd0 : lcnt + 1;
            if (lcnt == R - 1) lsec <= L_SCHED;
          end
          default: begin // L_SCHED（表への書き込みは上の always）
            lcnt <= lcnt + 1;
            if (lcnt == {s_reg[14:0], 1'b0} - 1) begin
              // 読み込み終わり。窓 -1（試行を流さず窓 0 の準備だけする）へ
              lsec <= L_HDR0;
              lcnt <= 16'd0;
              state <= ST_PRE;
              o <= 0;
              vnext <= 0;
              cur_valid <= 1'b0;
              prev_valid <= 1'b0;
              curbuf <= 1'b0;
              stage <= 0;
              sweep_cnt <= 0;
              sch_ra <= 0;
            end
          end
        endcase
      end

      // ======== 窓（-1、0 … WT-1、WT） ========
      if (in_win) begin
        o <= o + 1;
        if (state != ST_PRE) cycles <= cycles + 1;
        // 先読み
        if (o == O_ADJ) adj_ra <= vnext;
        if (o == O_NBI) begin
          for (i = 0; i < D; i = i + 1) begin
            nbidx[i] <= adj_rd2[i*NW +: NW];
            fwdn[i]  <= cur_valid && (adj_rd2[i*NW +: NW] == vcur);
          end
        end
        if (o == O_WB && prev_valid) begin
          col_we <= 1'b1;
          col_wa <= vprev;
          col_wd <= own_prev;
        end
        for (i = 0; i < D; i = i + 1)
          if (o == O_RD0 + i) col_ra <= nbidx[i];
        if (o == O_RD0 + D) col_ra <= vnext;
        for (i = 0; i < D; i = i + 1)
          if (o == O_GET0 + i) nbn[i] <= col_rd2;
        if (o == O_GET0 + D) ownn <= col_rd2;

        // 窓の境目
        if (win_end) begin
          vprev <= vcur;
          vcur  <= vnext;
          vnext <= (vnext == n_reg - 1) ? 0 : vnext + 1;
          prev_valid <= cur_valid;
          for (i = 0; i < D; i = i + 1) nb[i] <= nbn[i];
          fwd <= fwdn;
          if (curbuf) own0 <= ownn; else own1 <= ownn;
          curbuf <= ~curbuf;
          case (state)
            ST_PRE: begin
              state <= ST_RUN;
              cur_valid <= 1'b1;
              t_cur <= 32'd0;
              cycles <= 64'd0;
              th0 <= sch_th[31:0];        // 窓 -1 の間ずっと番地 0 を読んでいる
              th1 <= sch_th[63:32];
              cur_sweeps <= sch_sw;
              sch_ra <= 1;
            end
            ST_RUN: begin
              t_cur <= t_cur + 1;
              if (vcur == n_reg - 1) begin
                if (sweep_cnt + 1 == cur_sweeps) begin
                  sweep_cnt <= 0;
                  if (stage + 1 == s_reg) begin
                    state <= ST_POST;
                    cur_valid <= 1'b0;
                  end else begin
                    stage <= stage + 1;
                    th0 <= sch_th[31:0];      // = 段 stage+1
                    th1 <= sch_th[63:32];
                    cur_sweeps <= sch_sw;
                    sch_ra <= stage + 2;
                  end
                end else begin
                  sweep_cnt <= sweep_cnt + 1;
                end
              end
            end
            default: begin // ST_POST: 最後の頂点を書き戻し、パイプラインも抜けた
              state <= ST_OUT;
              gsec <= G_HDR;
              gcnt <= 16'd0;
              gsub <= 2'd0;
              gcs <= 0;
            end
          endcase
        end
      end

      // ======== 自分の色の反転（S3）。境目で書く OWN とは必ず別の側 ========
      for (kk = 0; kk < K; kk = kk + 1)
        if (s3_flip[kk]) begin
          if (s3_buf[kk]) own1[kk*P + s3_j[kk]] <= ~own1[kk*P + s3_j[kk]];
          else            own0[kk*P + s3_j[kk]] <= ~own0[kk*P + s3_j[kk]];
        end

      // ======== 出力 ========
      g1_v <= 1'b0;
      if (g_can) begin
        g1_v    <= 1'b1;
        g1_col  <= (gsec == G_COL);
        g1_d    <= g_word;
        g1_last <= g_last;
        g1_rep  <= (gsec == G_REP);
        g1_sub  <= gsub;
        rd_j    <= gcnt[JW-1:0];
        rd_k    <= (K == 1) ? {KW{1'b0}} : gcnt[JW +: KW];
        g1_cs   <= gcs;
        col_ra  <= gcnt[NW-1:0];
        case (gsec)
          G_HDR: begin
            gsub <= (gsub == 2'd2) ? 2'd0 : gsub + 1;
            if (gsub == 2'd2) gsec <= G_REP;
          end
          G_REP: begin
            gsub <= (gsub == 2'd2) ? 2'd0 : gsub + 1;
            if (gsub == 2'd2) begin
              gcnt <= (gcnt == R - 1) ? 16'd0 : gcnt + 1;
              if (gcnt == R - 1) gsec <= G_COL;
            end
          end
          default: begin
            gcs <= (gcs == CB - 1) ? 0 : gcs + 1;
            if (gcs == CB - 1) begin
              gcnt <= gcnt + 1;
              if (gcnt == n_reg - 1) gsec <= G_DONE;
            end
          end
        endcase
      end
      g2_v    <= g1_v;
      g2_col  <= g1_col;
      g2_d    <= g1_d;
      g2_rep  <= g1_rep;
      g2_sub  <= g1_sub;
      rd_k2   <= rd_k;
      g2_last <= g1_last;
      g2_cs   <= g1_cs;
      g3_v    <= g2_v;
      g3_col  <= g2_col;
      g3_d    <= g2_rep ? rep_word : g2_d;
      g3_last <= g2_last;
      g3_cs   <= g2_cs;

      if (f_push) begin
        fifo[f_wp] <= {g3_last, g3_col ? col_rd2p[64*g3_cs +: 64] : g3_d};
        f_wp <= f_wp + 1;
      end
      if (f_pop) f_rp <= f_rp + 1;
      f_cnt <= f_cnt + f_push - f_pop;

      if (o_load) begin
        m_axis_tvalid <= (f_cnt != 0);
        if (f_cnt != 0) {m_axis_tlast, m_axis_tdata} <= fifo[f_rp];
      end
      // 最後の語を渡し終えたら、次のジョブの見出しを待つ
      if (state == ST_OUT && m_axis_tvalid && m_axis_tready && m_axis_tlast)
        state <= ST_LOAD;
    end
  end

endmodule
