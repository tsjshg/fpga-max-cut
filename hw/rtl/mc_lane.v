`timescale 1ns / 1ps
// =====================================================================
//  mc_lane: 焼きなましのパイプライン 1 本（P 個のレプリカを1クロックずつ交互に流す）
//
//  段の割り当ては docs/m1-design.md の「レーン」の表。レプリカ j が S0 に入る時刻を T とすると
//    S0 (T)   乱数の状態 X[j] を読んで xorshift64 を1回進め、同じクロックで書き戻す
//    S1 (T+1) n_same を数える ／ r < TH0、r < TH1 を並べて比べる
//    S2 (T+2) 受け入れを決める ／ C[j] を読み、受け入れなら C + ΔC
//    S3 (T+3) C[j] を書く ／ 自分の色の反転を外へ出す ／ best[j] を読んで改善・到達を決める
//    S4 (T+4) best・best_t・hit_t を書く
//  同じレプリカの次の試行は P クロック後なので、レプリカごとの記憶（LUTRAM）は
//  「書いてから読む」までに必ず P−1 クロック以上空く。衝突を形のうえで起こさない。
//
//  データと有効ビットの段数を数え合わせるため、段 s のレジスタは名前の末尾を _s に揃える。
//
//  レプリカ交換法（pt_mode = 1、docs/m5.md）のときの追加:
//    - S0 でレプリカの温度の番号 tidx を読み（周回の偶奇で 2 組を使い分ける）、温度ごとのしきい値の表 thl から TH を引く
//    - S3 で、周回の最後の窓（s0_last）なら、C をスナップショット snap に書く（交換の判断に使う）
//    - tidx は交換の仕組み（mc_core）が書く。スナップショットは交換の仕組みが番地を渡して読む（1 段受けて出す）
// =====================================================================
module mc_lane #(
  parameter P  = 16,
  parameter JW = 4,          // log2(P)
  parameter D  = 4,
  parameter TW = 32          // 試行番号の幅。1 ジョブの窓の数 WT は 2^TW − 1 未満にする（全 1 を「無し」に使う）
) (
  input  wire          clk,

  // ---- S0 の入力（窓の制御から） ----
  input  wire          s0_valid,
  input  wire [JW-1:0] s0_j,
  input  wire [TW-1:0] s0_t,
  input  wire          s0_buf,      // この窓の OWN の番号
  input  wire [D-1:0]  s0_nb,       // 隣の色（このレーンのレプリカ j のぶん）
  input  wire          s0_own,      // 自分の色
  input  wire [31:0]   s0_th0,
  input  wire [31:0]   s0_th1,
  input  wire [15:0]   target,
  input  wire          pt_mode,
  input  wire          s0_bank,     // 交換法: この周回で使う tidx の組（周回の偶奇）
  input  wire          s0_last,     // 交換法: この窓が周回の最後の頂点

  // ---- S3 の出力: 自分の色の反転（OWN[s3_buf] のビット k·P + s3_j を反転） ----
  output reg           s3_flip,
  output wire          s3_buf_o,
  output wire [JW-1:0] s3_j_o,

  // ---- 読み込み（ジョブの始め）。レーンの中で1段レジスタに受けてから使う ----
  input  wire          ld_seed_we_i,
  input  wire          ld_c0_we_i,
  input  wire [JW-1:0] ld_j_i,
  input  wire [63:0]   ld_data_i,
  input  wire          ld_lth_we_i,  // 交換法: しきい値の表の行 ld_j に書く（全レーンに同じもの）

  // ---- 交換の仕組みとのやりとり（レーンの中で1段受ける） ----
  input  wire [JW-1:0] eng_snap_a_i, // スナップショットを読む番地（2 クロック後に snap_q に出る）
  output reg  [15:0]   snap_q,
  input  wire          eng_tw_we_i,  // tidx の組 eng_tw_bank の番地 eng_tw_a に eng_tw_d を書く
  input  wire          eng_tw_bank_i,
  input  wire [JW-1:0] eng_tw_a_i,
  input  wire [JW-1:0] eng_tw_d_i,
  input  wire          rd_bank,      // 結果を読み出すときの tidx の組
  output reg  [JW-1:0] rd_tidx,

  // ---- 結果の読み出し。rd_en の間は書き番地を rd_j にして書き口から読み、レーンの中で1段受ける
  //      （読み出し専用の口を別に持つと LUTRAM がもう1組要る。受けずに 48 レーンの選択までつなぐと、
  //       走りの番地 j_4 からの経路と見なされて K = 48 で WNS −2.6 ns になった）。rd_j の 1 クロック後に出る ----
  input  wire          rd_en_i,
  input  wire [JW-1:0] rd_j,
  output reg  [15:0]   rd_best,
  output reg  [15:0]   rd_c,
  output reg  [TW-1:0] rd_best_t,
  output reg  [TW-1:0] rd_hit_t,
  output reg           rd_bt_none,  // 改善が無い（best_t が全 1）
  output reg           rd_ht_none   // 届いていない（hit_t が全 1）
);

  // 読み込みの信号は全レーンに配られる大きな扇形なので、レーンごとに1段受ける
  // （K = 48 で、これが LUTRAM の書き番地の選択まで届いてタイミングを落としていた）。
  // 読み込みは窓 -1 より前に終わるので、1クロック遅れても走りには影響しない。
  reg          ld_seed_we, ld_c0_we, ld_lth_we;
  reg [JW-1:0] eng_snap_a;
  reg          eng_tw_we, eng_tw_bank;
  reg [JW-1:0] eng_tw_a, eng_tw_d;
  reg          rd_en;       // 結果を読み出す間 1（全レーンに配る信号なので1段受ける。出力の見出し3語の間に立つ）
  reg [JW-1:0] ld_j;
  reg [63:0]   ld_data;
  always @(posedge clk) begin
    ld_seed_we <= ld_seed_we_i;
    ld_c0_we   <= ld_c0_we_i;
    ld_j       <= ld_j_i;
    ld_data    <= ld_data_i;
    rd_en      <= rd_en_i;
    ld_lth_we  <= ld_lth_we_i;
    eng_snap_a <= eng_snap_a_i;
    eng_tw_we  <= eng_tw_we_i;
    eng_tw_bank <= eng_tw_bank_i;
    eng_tw_a   <= eng_tw_a_i;
    eng_tw_d   <= eng_tw_d_i;
  end

  // ---------------- レプリカごとの記憶 ----------------
  (* ram_style = "distributed" *) reg [63:0] xs_mem    [0:P-1];
  (* ram_style = "distributed" *) reg [15:0] c_mem     [0:P-1];
  (* ram_style = "distributed" *) reg [15:0] best_mem  [0:P-1];
  (* ram_style = "distributed" *) reg [TW-1:0] bestt_mem [0:P-1];
  (* ram_style = "distributed" *) reg [TW-1:0] hitt_mem  [0:P-1];
  (* ram_style = "distributed" *) reg        hitv_mem  [0:P-1];
  // 交換法
  (* ram_style = "distributed" *) reg [63:0] thl_mem   [0:P-1];   // 温度 k のしきい値 {TH1, TH0}
  (* ram_style = "distributed" *) reg [JW-1:0] tidx0_mem [0:P-1]; // 偶数の周回で使う温度の番号
  (* ram_style = "distributed" *) reg [JW-1:0] tidx1_mem [0:P-1]; // 奇数の周回で使う温度の番号
  (* ram_style = "distributed" *) reg [15:0] snap_mem  [0:P-1];   // 周回の終わりの C

  always @(posedge clk) begin
    if (ld_lth_we) thl_mem[ld_j] <= ld_data;
    // 読み込みでは両方の組を「レプリカ j は温度 j」にする
    if (ld_seed_we || (eng_tw_we && !eng_tw_bank)) tidx0_mem[ld_seed_we ? ld_j : eng_tw_a] <= ld_seed_we ? ld_j : eng_tw_d;
    if (ld_seed_we || (eng_tw_we &&  eng_tw_bank)) tidx1_mem[ld_seed_we ? ld_j : eng_tw_a] <= ld_seed_we ? ld_j : eng_tw_d;
    snap_q  <= snap_mem[eng_snap_a];
    rd_tidx <= rd_bank ? tidx1_mem[rd_j] : tidx0_mem[rd_j];
  end


  // ---------------- S0 ----------------
  wire [63:0] x0  = xs_mem[s0_j];
  wire [63:0] xa  = x0 ^ (x0 << 13);
  wire [63:0] xb  = xa ^ (xa >> 7);
  wire [63:0] xn0 = xb ^ (xb << 17);

  always @(posedge clk) begin
    if (ld_seed_we)    xs_mem[ld_j] <= ld_data;
    else if (s0_valid) xs_mem[s0_j] <= xn0;
  end

  // 交換法: このレプリカの温度の行のしきい値
  wire [JW-1:0] tidx0 = s0_bank ? tidx1_mem[s0_j] : tidx0_mem[s0_j];
  wire [63:0]   thl0  = thl_mem[tidx0];

  reg          v_1;
  reg [JW-1:0] j_1;
  reg [TW-1:0] t_1;
  reg          buf_1, own_1, last_1;
  reg [D-1:0]  nb_1;
  reg [31:0]   r_1, th0_1, th1_1;
  always @(posedge clk) begin
    v_1   <= s0_valid;
    j_1   <= s0_j;
    t_1   <= s0_t;
    buf_1 <= s0_buf;
    own_1 <= s0_own;
    nb_1  <= s0_nb;
    r_1   <= xn0[63:32];
    th0_1 <= pt_mode ? thl0[31:0]  : s0_th0;
    th1_1 <= pt_mode ? thl0[63:32] : s0_th1;
    last_1 <= s0_last;
  end

  // ---------------- S1 ----------------
  integer i;
  reg [2:0] ns1;
  always @(*) begin
    ns1 = 3'd0;
    for (i = 0; i < D; i = i + 1)
      ns1 = ns1 + (nb_1[i] == own_1);
  end

  reg          v_2;
  reg [JW-1:0] j_2;
  reg [TW-1:0] t_2;
  reg          buf_2, own_2, last_2;
  reg [2:0]    ns_2;
  reg          lt0_2, lt1_2;
  always @(posedge clk) begin
    v_2   <= v_1;
    j_2   <= j_1;
    t_2   <= t_1;
    buf_2 <= buf_1;
    own_2 <= own_1;
    ns_2  <= ns1;
    last_2 <= last_1;
    lt0_2 <= r_1 < th0_1;
    lt1_2 <= r_1 < th1_1;
  end

  // ---------------- S2 ----------------
  wire        ge2  = {ns_2, 1'b0} >= D;                 // ΔC = 2·n_same − D ≥ 0
  wire        acc2 = ge2 | (ns_2 == 3'd0 & lt0_2) | (ns_2 == 3'd1 & lt1_2);
  wire [15:0] dc2  = {12'd0, ns_2, 1'b0} - D;           // 16 bit の2の補数
  wire [15:0] c2   = c_mem[j_2];

  reg          v_3;
  reg [JW-1:0] j_3;
  reg [TW-1:0] t_3;
  reg          buf_3, own_3, acc_3, last_3;
  reg [15:0]   cn_3;
  always @(posedge clk) begin
    v_3   <= v_2;
    j_3   <= j_2;
    t_3   <= t_2;
    buf_3 <= buf_2;
    own_3 <= own_2;
    acc_3 <= acc2;
    last_3 <= last_2;
    cn_3  <= acc2 ? c2 + dc2 : c2;
  end

  // ---------------- S3 ----------------
  // C の書き口（番地 a3）。読み込み・結果の読み出しのときは番地を差し替える
  wire [JW-1:0] a3 = ld_c0_we ? ld_j : (rd_en ? rd_j : j_3);
  always @(posedge clk)
    if (ld_c0_we | v_3) c_mem[a3] <= ld_c0_we ? ld_data[15:0] : cn_3;

  // 交換法: 周回の最後の窓の C を残す（次の周回の間に交換の仕組みが読む）
  always @(posedge clk)
    if (v_3 & last_3) snap_mem[j_3] <= cn_3;

  always @(*) s3_flip = v_3 & acc_3;
  assign s3_buf_o = buf_3;
  assign s3_j_o   = j_3;

  wire imp3 = acc_3 & (cn_3 > best_mem[j_3]);
  wire hit3 = imp3 & ~hitv_mem[j_3] & (cn_3 >= target);

  reg          v_4, imp_4, hit_4;
  reg [JW-1:0] j_4;
  reg [TW-1:0] t_4;
  reg [15:0]   cn_4;
  always @(posedge clk) begin
    v_4   <= v_3;
    imp_4 <= imp3;
    hit_4 <= hit3;
    j_4   <= j_3;
    t_4   <= t_3;
    cn_4  <= cn_3;
  end

  // ---------------- S4 ----------------
  // 書き口（番地 a4）。結果の読み出しもここから
  wire [JW-1:0] a4 = ld_c0_we ? ld_j : (rd_en ? rd_j : j_4);
  always @(posedge clk) begin
    if (ld_c0_we) begin
      best_mem[a4]  <= ld_data[15:0];
      bestt_mem[a4] <= {TW{1'b1}};
      hitt_mem[a4]  <= {TW{1'b1}};
      hitv_mem[a4]  <= 1'b0;
    end else if (v_4) begin
      if (imp_4) begin
        best_mem[a4]  <= cn_4;
        bestt_mem[a4] <= t_4;
      end
      if (hit_4) begin
        hitt_mem[a4] <= t_4;
        hitv_mem[a4] <= 1'b1;
      end
    end
  end
  always @(posedge clk) begin
    rd_c       <= c_mem[a3];
    rd_best    <= best_mem[a4];
    rd_best_t  <= bestt_mem[a4];
    rd_hit_t   <= hitt_mem[a4];
    rd_bt_none <= &bestt_mem[a4];
    rd_ht_none <= &hitt_mem[a4];
  end

endmodule
