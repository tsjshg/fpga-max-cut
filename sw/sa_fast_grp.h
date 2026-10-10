/*
 * sa_fast_grp.h — sa_fast.c の「頂点ごとの語」のエンジンの本体。sa_fast.c から何度も読み込む。
 *
 * 読み込む前に決めておくもの:
 *   ENG   関数の名前
 *   VW    語の幅（64 bit の何本ぶんか）。1 なら uint64_t、2・4 なら GCC/clang のベクトル型
 *   RNGK  乱数の作り方:
 *           RNG_SCALAR  レプリカごとにスカラーの xorshift64
 *           RNG_NEON    NEON の 64 bit × 2 レーンで 2 レプリカずつ
 *           RNG_BSIN    状態をビットスライスで持ち、3 段のシフトを語の上で順に（その場で書き換え）
 *           RNG_BSFU    同じく、3 段を1段にまとめて古い状態から新しい状態を直接作る（xs_fused.h）
 *           RNG_BSAS    同じく、窓をレジスタに置いて流すアセンブリ（xs_fused.h。AArch64・ARMv7 とも
 *                       VW = 2 のとき。それ以外では RNG_BSFU と同じ）
 *   TRK   カット値の追い方:
 *           TRK_EV   ΔC ≠ 0 で受け入れたレプリカを1つずつ取り出して c を足し込む
 *           TRK_GAP  差 g = best − c の下 GL 桁をビットスライスで持ち、語のまま足し込む。
 *                    桁あふれ（上の桁の増減と改善）はまれなので、そこだけ1つずつ直す
 *   DC    次数を定数にするなら 3 や 4、実行時の d を使うなら 0
 *
 * W = 64·VW 個のレプリカを1組にし、組の全員が同じ頂点 v を同じ試行番号 t で処理する
 * （頂点は順に回すので、どのレプリカも試行 t の頂点は t % n で同じ）。
 * 塗り分けは回路と同じく COL[v] のビット i = 組の i 番目のレプリカの色。
 */
#define W (64 * VW)
#define VT CAT(vt_, ENG)
#define BS (RNGK == RNG_BSIN || RNGK == RNG_BSFU || RNGK == RNG_BSAS)
#if VW == 1
typedef uint64_t VT;
#define LN(x, l) (x)
#define ANY(x) ((x) != 0)
#else
typedef uint64_t VT __attribute__((vector_size(8 * VW)));
#define LN(x, l) ((x)[l])
#if VW == 2
#define ANY(x) (((x)[0] | (x)[1]) != 0)
#else
#define ANY(x) (((x)[0] | (x)[1] | (x)[2] | (x)[3]) != 0)
#endif
#endif
#define BIT(x, i) ((((const uint64_t *)&(x))[(i) / 64] >> ((i) % 64)) & 1)
#define SETBIT(x, i) (((uint64_t *)&(x))[(i) / 64] |= 1ull << ((i) % 64))

#if RNGK == RNG_BSFU || RNGK == RNG_BSAS
/* 新旧が別の配列だとコンパイラに伝える（でないと書くたびに古い状態を読み直す） */
static inline __attribute__((always_inline)) void CAT(xs_, ENG)(VT *restrict w, const VT *restrict x)
{
#if RNGK == RNG_BSAS && defined(__aarch64__) && VW == 2
    XS_A64(w, x);
#elif RNGK == RNG_BSAS && defined(__arm__) && defined(__ARM_NEON) && VW == 2
    XS_A32(w, x);                                         /* レプリカ 0..63 */
    XS_A32((char *)w + 8, (const char *)x + 8);          /* レプリカ 64..127 */
#else
    XS_FUSED(w, x);
#endif
}
#endif

static void ENG(int gi)
{
    const int D = DC ? DC : d;
    const int K = (D + 1) / 2;                          /* ns < K なら ΔC < 0（しきい値を引く） */
    const int NB = D < 2 ? 1 : D < 4 ? 2 : D < 8 ? 3 : 4; /* ns（0..D）のビット数 */
    VT *COL = xalloc64(sizeof(VT) * n);
    grp_t *g = grp_new(gi, W);
    memset(COL, 0, sizeof(VT) * n);
    for (int i = 0; i < g->nv; i++)
        for (int v = 0; v < n; v++)
            if (g->s0[(size_t)i * n + v])
                SETBIT(COL[v], i);

#if BS
    /* XB[0]・XB[1] を交互に使う（BSIN は XB[0] だけ）。X[b] のビット i = レプリカ i の状態のビット b */
    VT(*XB)[64] = xalloc64(sizeof(VT) * 128);
    VT *X = XB[0];
    memset(XB, 0, sizeof(VT) * 128);
    for (int i = 0; i < W; i++)
        for (int b = 0; b < 64; b++)
            if ((g->x[i] >> b) & 1)
                SETBIT(X[b], i);
#elif RNGK == RNG_NEON
    /* 2 本ずつ: st[2j] = レプリカ j、st[2j+1] = レプリカ j+32（比べた結果を 32 bit の2レーンに置くため） */
    uint64_t st[64] __attribute__((aligned(16)));
    for (int j = 0; j < 32; j++) {
        st[2 * j] = g->x[j];
        st[2 * j + 1] = g->x[j + 32];
    }
#else
    uint64_t st[W];
    memcpy(st, g->x, sizeof st);
#endif

#if TRK == TRK_GAP
    /* g = best − c ≥ 0 を、下 GL 桁 Lo（ビットスライス、0..2^GL−1）と上の桁 H（レプリカごと）に分けて持つ。
     * 毎試行 Lo += δ を GL+1 桁で計算し、桁 GL が立ったレプリカ（Lo があふれたか負になった）だけ
     * 1つずつ直す: 2^GL 以上なら H を1増やす。負なら H > 0 なら H を1減らし、H = 0 なら g < 0、
     * つまり best を超えた（改善）。あふれは数百試行に1回なので、ここの枝はまず通らない */
    const int DB = NB + 2; /* δ = D − 2·ns（−D..D）の桁数。これより上は符号の桁 */
    VT GP[GL + 1];
    for (int j = 0; j <= GL; j++)
        GP[j] = (VT){0};
    int32_t H[W];
    memset(H, 0, sizeof H);
#endif

    uint64_t t = 0;
    for (int k = 0; k < S; k++) {
        const uint32_t *T = th + k * (d + 1);
#if BS
        /* 乱数 r（状態の上位 32 bit）と、レプリカごとに選んだしきい値 TH[ns] を上の桁から比べる。
         * 桁 b でレプリカが使うしきい値のビットは、どの TH[q] が 1 かの組 sel[b] で決まる。
         * どのしきい値でも 0 の上位 z 桁は「r のその桁が 0 か」を見るだけでよい。 */
        uint32_t thor = 0;
        uint8_t sel[32];
        for (int b = 0; b < 32; b++) {
            sel[b] = 0;
            for (int q = 0; q < K; q++)
                sel[b] |= ((T[q] >> b) & 1) << q;
        }
        for (int q = 0; q < K; q++)
            thor |= T[q];
        const int z = __builtin_clz(thor | 1);
        const int stop0 = 32 - (z > BS_F ? z : BS_F); /* ここまで一気に比べ、決まらなければ続ける */
#elif RNGK == RNG_SCALAR
        uint64_t THS[8];
        for (int j = 0; j < K; j++)
            THS[j] = (uint64_t)T[j] << 32; /* r < TH ⇔ x < TH·2^32 */
#endif
        for (uint32_t m = 0; m < sweeps[k]; m++)
            for (int v = 0; v < n; v++, t++) {
#if BS
                /* 乱数を1回進める（隣の色によらないので先に。比べるのは後） */
#if RNGK == RNG_BSIN
                for (int b = 63; b >= 13; b--)
                    X[b] ^= X[b - 13];
                for (int b = 0; b <= 56; b++)
                    X[b] ^= X[b + 7];
                for (int b = 63; b >= 17; b--)
                    X[b] ^= X[b - 17];
#else
                {
                    VT *Xn = X == XB[0] ? XB[1] : XB[0];
                    CAT(xs_, ENG)(Xn, X);
                    X = Xn;
                }
#endif
#endif
                const uint32_t *nb = adj + v * D;
                VT own = COL[v];
                /* 同じ色の隣の数 ns をビットごとに数える（P[b] のビット i = レプリカ i の ns のビット b） */
                VT P[4] = {0};
                for (int j = 0; j < D; j++) {
                    VT e = ~(COL[nb[j]] ^ own);
                    for (int b = 0; b < NB; b++) {
                        VT cy = P[b] & e;
                        P[b] ^= e;
                        e = cy;
                    }
                }
                VT EQ[16];
                for (int q = 0; q <= D; q++) {
                    VT e = ~(VT){0};
                    for (int b = 0; b < NB; b++)
                        e &= ((q >> b) & 1) ? P[b] : ~P[b];
                    EQ[q] = e;
                }
                VT NEG = {0}; /* ΔC < 0 のレプリカ */
                for (int q = 0; q < K; q++)
                    NEG |= EQ[q];

                /* 乱数を1回進め、LT = (r < TH[ns]) を作る（ΔC < 0 のレプリカについてだけ意味がある） */
                VT LT = {0};
#if BS
                {
                    VT SEL[1 << BS_KMAX];
                    SEL[0] = (VT){0};
                    for (int q = 1; q < (1 << K); q++)
                        SEL[q] = SEL[q & (q - 1)] | EQ[__builtin_ctz(q)];
                    VT eq = NEG; /* r の上の桁がここまで TH[ns] と等しいレプリカ */
                    int b = 31;
                    for (; b >= 32 - z; b--)
                        eq &= ~X[32 + b];
                    int stop = stop0;
                    for (;;) {
                        for (; b >= stop; b--) {
                            VT r = X[32 + b];
                            VT tb = SEL[sel[b]];
                            LT |= eq & tb & ~r;
                            eq &= ~(r ^ tb);
                        }
                        if (__builtin_expect(!ANY(eq), 1) || b < 0)
                            break;
                        stop = b; /* まだ等しいレプリカがいる（まれ）。1 桁ずつ続ける */
                    }
                }
#elif RNGK == RNG_NEON
                {
                    uint32x2_t acc[8];
                    uint32x2_t thv[8];
                    for (int q = 0; q < K; q++) {
                        acc[q] = vdup_n_u32(0);
                        thv[q] = vdup_n_u32(T[q]);
                    }
                    for (int j = 0; j < 32; j++) {
                        uint64x2_t x = vld1q_u64(st + 2 * j);
                        x = veorq_u64(x, vshlq_n_u64(x, 13));
                        x = veorq_u64(x, vshrq_n_u64(x, 7));
                        x = veorq_u64(x, vshlq_n_u64(x, 17));
                        vst1q_u64(st + 2 * j, x);
                        uint32x2_t r = vshrn_n_u64(x, 32);
                        uint32x2_t bit = vdup_n_u32(1u << j);
                        for (int q = 0; q < K; q++)
                            acc[q] = vorr_u32(acc[q], vand_u32(vclt_u32(r, thv[q]), bit));
                    }
                    for (int q = 0; q < K; q++)
                        LT |= EQ[q] & vget_lane_u64(vreinterpret_u64_u32(acc[q]), 0);
                }
#else
                {
                    uint64_t lt[8] = {0};
                    /* clang はこのループを NEON 2 レーンに自動でベクトル化する。NEON の効果を
                     * 別に測るため（RNG_NEON）、ここではさせない */
#ifdef __clang__
#pragma clang loop vectorize(disable) interleave(disable)
#endif
                    for (int i = 0; i < W; i++) {
                        uint64_t x = st[i];
                        x ^= x << 13;
                        x ^= x >> 7;
                        x ^= x << 17;
                        st[i] = x;
                        for (int q = 0; q < K; q++)
                            lt[q] |= (uint64_t)(x < THS[q]) << i;
                    }
                    for (int q = 0; q < K; q++)
                        LT |= EQ[q] & lt[q];
                }
#endif
                VT ACC = ~NEG | LT;
                COL[v] = own ^ ACC;

#if TRK == TRK_GAP
                /* g += δ（受け入れたレプリカは δ = D − 2·ns、他は 0）。δ の桁 j は、
                 * δ_q = D − 2q の桁 j が 1 の q について「ns = q かつ受け入れ」を OR したもの */
                {
                    VT DL[8];
                    for (int j = 0; j < DB; j++) {
                        VT e = {0};
                        for (int q = 0; q <= D; q++)
                            if (((D - 2 * q) >> j) & 1)
                                e |= q < K ? EQ[q] & LT : EQ[q];
                        DL[j] = e;
                    }
                    VT cy = {0};
                    for (int j = 0; j <= GL; j++) {
                        VT a = GP[j], b = DL[j < DB ? j : DB - 1];
                        VT s = a ^ b;
                        GP[j] = s ^ cy;
                        cy = (a & b) | (cy & s);
                    }
                    VT WR = GP[GL]; /* Lo + δ が 0..2^GL−1 に収まらなかった */
                    if (__builtin_expect(ANY(WR), 0)) {
                        for (int l = 0; l < VW; l++) {
                            uint64_t mk = LN(WR, l);
                            while (mk) {
                                int i = l * 64 + __builtin_ctzll(mk);
                                mk &= mk - 1;
                                int32_t lo = 0;
                                for (int j = 0; j < GL; j++)
                                    lo |= (int32_t)BIT(GP[j], i) << j;
                                if (BIT(DL[DB - 1], i)) { /* δ < 0: 負になった */
                                    if (H[i] > 0) {
                                        H[i]--;
                                    } else {
                                        /* g = lo − 2^GL < 0: best を超えた。g = 0 にする */
                                        track_update(&g->tr[i], g->tr[i].best - (lo - (1 << GL)), t);
                                        for (int j = 0; j < GL; j++)
                                            ((uint64_t *)&GP[j])[l] &= ~(1ull << (i % 64));
                                    }
                                } else {
                                    H[i]++;
                                }
                            }
                        }
                        GP[GL] = (VT){0};
                    }
                }
#else
                /* カット値が変わるレプリカ（受け入れ、かつ ΔC ≠ 0）だけを1つずつ足し込む */
                VT EV = (D & 1) ? ACC : ACC & ~EQ[D / 2];
                for (int l = 0; l < VW; l++) {
                    uint64_t mk = LN(EV, l);
                    while (mk) {
                        int bi = __builtin_ctzll(mk);
                        mk &= mk - 1;
                        int i = l * 64 + bi;
                        int ns = 0;
                        for (int b = 0; b < NB; b++)
                            ns |= (int)((LN(P[b], l) >> bi) & 1) << b;
                        grp_add(g, i, 2 * ns - D, t);
                    }
                }
#endif
            }
    }
    for (int i = 0; i < g->nv; i++)
        for (int v = 0; v < n; v++)
            g->s[(size_t)i * n + v] = BIT(COL[v], i);
#if TRK == TRK_GAP
    /* 最後の c は best − g。これを結果にし、grp_done で数え直した値と突き合わせる */
    for (int i = 0; i < g->nv; i++) {
        int32_t lo = 0;
        for (int j = 0; j < GL; j++)
            lo |= (int32_t)BIT(GP[j], i) << j;
        g->c[i] = g->tr[i].best - ((H[i] << GL) + lo);
    }
#endif
    grp_done(g);
    free(COL);
#if BS
    free(XB);
#endif
}

#undef W
#undef VT
#undef BS
#undef LN
#undef ANY
#undef BIT
#undef SETBIT
#undef ENG
#undef VW
#undef RNGK
#undef TRK
#undef DC
