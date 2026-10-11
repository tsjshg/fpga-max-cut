/*
 * sa_gpu.metal — sa.c の焼きなましを Mac の GPU で回すカーネル（規則は sw/sa.c と同じ）。
 *
 * ホスト（sa_gpu.m）が実行時に次の定数を頭に足してからコンパイルする:
 *   D   次数（2..8）            N   頂点の数
 *   K   (D+1)/2（ns < K なら ΔC < 0 でしきい値を引く）
 *   NB  ns（0..D）のビット数     NT  目標の数（0..8）
 *   GL  エンジン b の差 g の下の桁数
 *
 * エンジン a: 1 スレッド = 1 レプリカ。SIMD グループの 32 レーン（32 レプリカ）で、頂点ごとの
 *   32 bit の語（ビット lane = レーン lane のレプリカの色）を threadgroup メモリに置いて共有する。
 *   頂点は順に回すので、32 レーンは同じ頂点を同時に処理する。隣の語は全レーンで同じ番地を読む。
 *   受け入れは simd_ballot で語にし、全レーンが同じ値を書き戻す。乱数はレーンごとの xorshift64。
 * エンジン b: sa_fast と同じビットスライス。1 スレッドが 32 レプリカを持ち、頂点ごとの語
 *   （device メモリ、[v][スレッド]）と、xorshift64 の状態を「ビットごとの語 64 本」で持つ。
 *   xorshift64 は語の XOR 155 回。r < TH[ns] は語のまま上の桁から比べる。カット値は
 *   差 g = best − c の下 GL 桁をビットスライスで足し込み、桁あふれだけ 1 つずつ直す（sa_fast の bsasg）。
 *
 * どちらも 1 回の dispatch で「段と周回数の組」の区間 [seg0, seg1) を回し、状態は device メモリに残す
 * （長い実行は dispatch を分ける）。試行番号 t は 64 bit（tb + dispatch 内の 32 bit の数）。
 */
#include <metal_stdlib>
using namespace metal;

struct Params {
    uint seg0, seg1;   /* この dispatch で回す段の組 [seg0, seg1) */
    uint tb_lo, tb_hi; /* この dispatch の最初の試行番号 */
    uint nth;          /* エンジン b: スレッドの総数（語の並びの幅） */
    uint pad;
};

#ifndef ADEV
#define ADEV 0
#endif
#define TB(P) (((ulong)(P).tb_hi << 32) | (ulong)(P).tb_lo)

/* ================= エンジン a ================= */

/* 状態: xs[p] = xorshift64（lo, hi）、st[p] = (c, best, 次の目標の番号, 0)、bt[p] = best_t、
 * hit[p*NT + j]、col[group*N + v]（group = p / 32） */
kernel void sa_a(device uint *col [[buffer(0)]],
                 device uint2 *xs [[buffer(1)]],
                 device int4 *st [[buffer(2)]],
                 device ulong *bt [[buffer(3)]],
                 device ulong *hit [[buffer(4)]],
                 constant ushort *adj [[buffer(5)]],
                 constant uint *th [[buffer(6)]],
                 constant uint2 *seg [[buffer(7)]],
                 constant int *tgt [[buffer(8)]],
                 constant Params &P [[buffer(9)]],
                 threadgroup uint *tgm [[threadgroup(0)]],
                 uint gid [[thread_position_in_grid]],
                 uint lane [[thread_index_in_simdgroup]],
                 uint sg [[simdgroup_index_in_threadgroup]])
{
    device uint *cg = col + (gid / 32) * N;
#if ADEV
    device uint *w = cg; /* 語を device メモリのまま使う（キャッシュ任せ） */
#else
    threadgroup uint *w = tgm + sg * N;
    for (uint v = lane; v < N; v += 32)
        w[v] = cg[v];
    simdgroup_barrier(mem_flags::mem_threadgroup);
#endif

#if APF
    /* 先読み: 頂点 v を処理するとき、v+1 の語（自分と隣）と v+2 の隣の番号はもう読んである。
     * メモリの読みは 1 回 約 46 ns かかり、SIMD グループは命令を順にしか出せないので、使う 1 頂点前に読む。
     * v+1 の語は v を書き戻す前に読むので、v+1 の隣に v があれば書き戻した値に直す */
    uint pw = w[0], pn[D];
    ushort i1[D], i2[D];
    for (int j = 0; j < D; j++) {
        pn[j] = w[adj[j]];
        i1[j] = adj[(1 % N) * D + j];
    }
    uint v2 = 2 % N; /* 次に番号を読む頂点 */
#endif
    uint lo = xs[gid].x, hi = xs[gid].y;
    int4 s4 = st[gid];
    int c = s4.x, best = s4.y, nxt = s4.z;
    ulong tb = TB(P);
    uint tl = 0;
#ifdef A_NOTG
    uint wreg = w[0];
#endif
    for (uint si = P.seg0; si < P.seg1; si++) {
        uint2 sg2 = seg[si];
        constant uint *T = th + sg2.x * (D + 1);
        uint T0 = T[0];
#if K > 1
        uint T1 = T[1];
#endif
#if K > 2
        uint T2 = T[2];
#endif
#if K > 3
        uint T3 = T[3];
#endif
        for (uint m = 0; m < sg2.y; m++) {
            constant ushort *nb = adj;
            for (uint v = 0; v < N; v++, tl++, nb += D) {
#if defined(NO_RNG)
                hi ^= lo + v;
#else
                /* xorshift64 を 32 bit の 2 語で */
                hi ^= (hi << 13) | (lo >> 19);
                lo ^= lo << 13;
                lo ^= (lo >> 7) | (hi << 25);
                hi ^= hi >> 7;
                hi ^= (hi << 17) | (lo >> 15);
                lo ^= lo << 17;
#endif
#if APF
                uint wv = pw, nw[D];
                for (int j = 0; j < D; j++) {
                    nw[j] = pn[j];
                    i2[j] = i1[j];
                }
                /* v+1 の語を読み、v+2 の番号を読む */
                pw = w[v + 1 == N ? 0 : v + 1];
                for (int j = 0; j < D; j++)
                    pn[j] = w[i1[j]];
                for (int j = 0; j < D; j++)
                    i1[j] = adj[v2 * D + j];
                v2 = v2 + 1 == N ? 0 : v2 + 1;
                int nd = 0; /* 違う色の隣の数 */
                for (int j = 0; j < D; j++)
                    nd += (int)extract_bits(nw[j] ^ wv, lane, 1);
#elif defined(A_NOTG)
                uint wv = wreg;
                int nd = 0;
                for (int j = 0; j < D; j++)
                    nd += (int)extract_bits((wv >> nb[j]) ^ wv, lane, 1);
#else
                uint wv = w[v];
                int nd = 0; /* 違う色の隣の数 */
                for (int j = 0; j < D; j++)
                    nd += (int)extract_bits(w[nb[j]] ^ wv, lane, 1);
#endif
                int ns = D - nd;
                int dc = 2 * ns - D;
                uint thr = T0;
#if K > 1
                thr = ns == 1 ? T1 : thr;
#endif
#if K > 2
                thr = ns == 2 ? T2 : thr;
#endif
#if K > 3
                thr = ns == 3 ? T3 : thr;
#endif
                bool acc = dc >= 0 || hi < thr;
#ifdef A_NOBALLOT
                uint mk = acc ? (1u << lane) : 0u;
#else
                uint mk = (uint)(ulong)(simd_vote::vote_t)simd_ballot(acc);
#endif
#ifdef A_NOTG
                wreg = wv ^ mk;
#else
                w[v] = wv ^ mk; /* 全レーンが同じ値を書く */
#endif
#if APF
                for (int j = 0; j < D; j++)
                    pn[j] = i2[j] == v ? wv ^ mk : pn[j]; /* i2 = v+1 の隣の番号 */
#endif
                c += acc ? dc : 0;
                if (c > best) {
                    best = c;
                    ulong t = tb + tl;
                    bt[gid] = t;
                    while (nxt < NT && c >= tgt[nxt]) {
                        hit[gid * (NT > 0 ? NT : 1) + nxt] = t;
                        nxt++;
                    }
                }
            }
        }
    }
#ifdef A_NOTG
    w[0] = wreg;
#endif
    xs[gid] = uint2(lo, hi);
    st[gid] = int4(c, best, nxt, 0);
#if !ADEV
    simdgroup_barrier(mem_flags::mem_threadgroup);
    for (uint v = lane; v < N; v += 32)
        cg[v] = w[v];
#endif
}

/* カット値を数える（最初と最後）。cnt[p] */
kernel void count_a(device const uint *col [[buffer(0)]],
                    constant ushort *adj [[buffer(5)]],
                    device int *cnt [[buffer(10)]],
                    uint gid [[thread_position_in_grid]],
                    uint lane [[thread_index_in_simdgroup]])
{
    device const uint *cg = col + (gid / 32) * N;
    int c = 0;
    for (uint v = 0; v < N; v++) {
        uint wv = cg[v];
        for (int j = 0; j < D; j++)
            c += (int)extract_bits(cg[adj[v * D + j]] ^ wv, lane, 1);
    }
    cnt[gid] = c / 2;
}

/* ================= エンジン b ================= */

/* 状態（スレッド q、そのレプリカ p = 32q + i）:
 *   col[v*nth + q]、xb[b*nth + q]（xorshift64 の状態のビット b）、gp[j*nth + q]（g の下の桁 j）、
 *   hh[p]（g の上の桁）、best[p]、nx[p]（次の目標の番号）、bt[p]、hit[p*NT + j] */
kernel void sa_b(device uint *col [[buffer(0)]],
                 device uint *xb [[buffer(1)]],
                 device uint *gp [[buffer(2)]],
                 device int *hh [[buffer(3)]],
                 device int *best [[buffer(4)]],
                 device int *nx [[buffer(5)]],
                 device ulong *bt [[buffer(6)]],
                 device ulong *hit [[buffer(7)]],
                 constant uint *adjm [[buffer(8)]], /* 隣の番号 × nth（語の番地の差） */
                 constant uint *th [[buffer(9)]],
                 constant uint2 *seg [[buffer(10)]],
                 constant int *tgt [[buffer(11)]],
                 constant Params &P [[buffer(12)]],
                 constant uint *zs [[buffer(13)]],
                 uint q [[thread_position_in_grid]])
{
    const uint nth = P.nth;
    uint X[64];
    for (int b = 0; b < 64; b++)
        X[b] = xb[b * nth + q];
#if GW
    /* 全部の桁: g' = g / 2^HALF を GW 桁の 2 の補数で持つ（d が偶数なら g は常に偶数なので半分にする） */
    uint G[GW];
    for (int j = 0; j < GW; j++)
        G[j] = gp[j * nth + q];
#else
    uint G[GL + 1];
    for (int j = 0; j < GL; j++)
        G[j] = gp[j * nth + q];
    G[GL] = 0;
#endif
    device uint *cq = col + q;
    const int DB = NB + 2; /* δ = D − 2·ns（−D..D）の桁数 */
    const int HALF = (D & 1) ? 0 : 1;
    const int DBF = (D >> HALF) < 2 ? 2 : (D >> HALF) < 4 ? 3 : (D >> HALF) < 8 ? 4 : 5; /* δ' の 2 の補数の桁数 */
    ulong tb = TB(P);
    uint tl = 0;
    for (uint si = P.seg0; si < P.seg1; si++) {
        uint2 sg2 = seg[si];
        constant uint *T = th + sg2.x * (D + 1);
        uint TQ[K];
        for (int k = 0; k < K; k++)
            TQ[k] = T[k];
        const int z = (int)zs[sg2.x]; /* どのしきい値でも 0 の上の桁の数 */
        for (uint m = 0; m < sg2.y; m++) {
            constant uint *nb = adjm;
            device uint *cv = cq;
            for (uint v = 0; v < N; v++, tl++, nb += D, cv += nth) {
                /* xorshift64（語の XOR 155 回） */
#ifndef NO_RNG
#pragma clang loop unroll(full)
                for (int b = 63; b >= 13; b--)
                    X[b] ^= X[b - 13];
#pragma clang loop unroll(full)
                for (int b = 0; b <= 56; b++)
                    X[b] ^= X[b + 7];
#pragma clang loop unroll(full)
                for (int b = 63; b >= 17; b--)
                    X[b] ^= X[b - 17];
#else
                X[63] ^= v;
                X[62] += v;
#endif

                uint own = *cv;
                uint Pb[NB];
                for (int b = 0; b < NB; b++)
                    Pb[b] = 0;
                for (int j = 0; j < D; j++) {
                    uint e = ~(cq[nb[j]] ^ own);
                    for (int b = 0; b < NB; b++) {
                        uint cy = Pb[b] & e;
                        Pb[b] ^= e;
                        e = cy;
                    }
                }
                uint EQ[D + 1];
                for (int k = 0; k <= D; k++) {
                    uint e = ~0u;
                    for (int b = 0; b < NB; b++)
                        e &= ((k >> b) & 1) ? Pb[b] : ~Pb[b];
                    EQ[k] = e;
                }
                uint NEG = 0;
                for (int k = 0; k < K; k++)
                    NEG |= EQ[k];

                /* LT = (r < TH[ns])（ΔC < 0 のレプリカについてだけ意味がある）。上の z 桁はどのしきい値でも 0 なので、
                 * 「r のその桁が全部 0 か」だけを見る（OR をまとめてとる）。残りの桁は上から比べ、32 スレッドの
                 * どのレプリカも決まったら飛ばす。z は段ごとに一様なので、展開した列の途中へ switch で飛び込む */
                uint eq = NEG, LT = 0;
#ifdef NO_CMP
                LT = X[32 + 31] & X[32 + 30];
#else
                uint lead = 0;
                switch (z) {
                    case 32: lead |= X[32];
                    case 31: lead |= X[33];
                    case 30: lead |= X[34];
                    case 29: lead |= X[35];
                    case 28: lead |= X[36];
                    case 27: lead |= X[37];
                    case 26: lead |= X[38];
                    case 25: lead |= X[39];
                    case 24: lead |= X[40];
                    case 23: lead |= X[41];
                    case 22: lead |= X[42];
                    case 21: lead |= X[43];
                    case 20: lead |= X[44];
                    case 19: lead |= X[45];
                    case 18: lead |= X[46];
                    case 17: lead |= X[47];
                    case 16: lead |= X[48];
                    case 15: lead |= X[49];
                    case 14: lead |= X[50];
                    case 13: lead |= X[51];
                    case 12: lead |= X[52];
                    case 11: lead |= X[53];
                    case 10: lead |= X[54];
                    case 9: lead |= X[55];
                    case 8: lead |= X[56];
                    case 7: lead |= X[57];
                    case 6: lead |= X[58];
                    case 5: lead |= X[59];
                    case 4: lead |= X[60];
                    case 3: lead |= X[61];
                    case 2: lead |= X[62];
                    case 1: lead |= X[63];
                    default: break;
                }
                eq &= ~lead;
#define CMPB(b)                                                                     \
    if (eq != 0) {                                                                   \
        uint r = X[32 + (b)], tbit = 0;                                              \
        for (int k = 0; k < K; k++)                                                  \
            tbit |= EQ[k] & (uint)((int)(TQ[k] << (31 - (b))) >> 31);                \
        LT |= eq & tbit & ~r;                                                        \
        eq &= ~(r ^ tbit);                                                           \
    }
                switch (z) {
                    case 0: CMPB(31);
                    case 1: CMPB(30);
                    case 2: CMPB(29);
                    case 3: CMPB(28);
                    case 4: CMPB(27);
                    case 5: CMPB(26);
                    case 6: CMPB(25);
                    case 7: CMPB(24);
                    case 8: CMPB(23);
                    case 9: CMPB(22);
                    case 10: CMPB(21);
                    case 11: CMPB(20);
                    case 12: CMPB(19);
                    case 13: CMPB(18);
                    case 14: CMPB(17);
                    case 15: CMPB(16);
                    case 16: CMPB(15);
                    case 17: CMPB(14);
                    case 18: CMPB(13);
                    case 19: CMPB(12);
                    case 20: CMPB(11);
                    case 21: CMPB(10);
                    case 22: CMPB(9);
                    case 23: CMPB(8);
                    case 24: CMPB(7);
                    case 25: CMPB(6);
                    case 26: CMPB(5);
                    case 27: CMPB(4);
                    case 28: CMPB(3);
                    case 29: CMPB(2);
                    case 30: CMPB(1);
                    case 31: CMPB(0);
                    default: break;
                }
#undef CMPB
#endif
                uint ACC = ~NEG | LT;
                *cv = own ^ ACC;

#ifndef NO_TRK
#if GW
                /* g' += δ'（受け入れたレプリカだけ δ' = (D − 2·ns) / 2^HALF）。g' < 0 になったら best を超えた（改善）。
                 * 改善したレプリカだけ 1 つずつ best・best_t・目標を直し、g' = 0 にする */
                {
                    uint DL[DBF];
                    for (int j = 0; j < DBF; j++) {
                        uint e = 0;
                        for (int k = 0; k <= D; k++)
                            if ((((D - 2 * k) >> HALF) >> j) & 1)
                                e |= k < K ? EQ[k] & LT : EQ[k];
                        DL[j] = e;
                    }
                    uint cy = 0;
                    for (int j = 0; j < GW; j++) {
                        uint a = G[j], bb = DL[j < DBF ? j : DBF - 1];
                        uint s = a ^ bb;
                        G[j] = s ^ cy;
                        cy = (a & bb) | (cy & s);
                    }
                    uint NEGM = G[GW - 1];
#ifdef NO_SLOW
                    NEGM = 0;
#endif
                    if (NEGM != 0) {
                        uint mk = NEGM;
                        while (mk) {
                            uint i = ctz(mk);
                            mk &= mk - 1;
                            uint p = q * 32 + i;
                            int lo = 0; /* g' は −D/2^HALF..−1。下 DBF 桁が 2 の補数 */
                            for (int j = 0; j < DBF; j++)
                                lo |= (int)((G[j] >> i) & 1) << j;
                            int c = best[p] - ((lo - (1 << DBF)) << HALF);
                            best[p] = c;
                            ulong t = tb + tl;
                            bt[p] = t;
                            int nt = nx[p];
                            if (nt < NT && c >= tgt[nt]) {
                                while (nt < NT && c >= tgt[nt]) {
                                    hit[p * (NT > 0 ? NT : 1) + nt] = t;
                                    nt++;
                                }
                                nx[p] = nt;
                            }
                        }
                        for (int j = 0; j < GW; j++)
                            G[j] &= ~NEGM;
                    }
                }
#else
                /* g += δ（受け入れたレプリカだけ δ = D − 2·ns） */
                uint DL[DB];
                for (int j = 0; j < DB; j++) {
                    uint e = 0;
                    for (int k = 0; k <= D; k++)
                        if (((D - 2 * k) >> j) & 1)
                            e |= k < K ? EQ[k] & LT : EQ[k];
                    DL[j] = e;
                }
                uint cy = 0;
                for (int j = 0; j <= GL; j++) {
                    uint a = G[j], bb = DL[j < DB ? j : DB - 1];
                    uint s = a ^ bb;
                    G[j] = s ^ cy;
                    cy = (a & bb) | (cy & s);
                }
                uint WR = G[GL];
#ifdef NO_SLOW
                G[GL] = 0;
                WR = 0;
#endif
                if (WR != 0) {
                    uint mk = WR;
                    while (mk) {
                        uint i = ctz(mk);
                        mk &= mk - 1;
                        uint p = q * 32 + i;
                        if ((DL[DB - 1] >> i) & 1) { /* δ < 0: 負になった */
                            int h = hh[p];
                            if (h > 0) {
                                hh[p] = h - 1;
                            } else {
                                /* g = lo − 2^GL < 0: best を超えた。g = 0 にする */
                                int lo = 0;
                                for (int j = 0; j < GL; j++)
                                    lo |= (int)((G[j] >> i) & 1) << j;
                                int c = best[p] - (lo - (1 << GL));
                                best[p] = c;
                                ulong t = tb + tl;
                                bt[p] = t;
                                int nt = nx[p];
                                if (nt < NT && c >= tgt[nt]) {
                                    while (nt < NT && c >= tgt[nt]) {
                                        hit[p * (NT > 0 ? NT : 1) + nt] = t;
                                        nt++;
                                    }
                                    nx[p] = nt;
                                }
                                for (int j = 0; j < GL; j++)
                                    G[j] &= ~(1u << i);
                            }
                        } else {
                            hh[p] += 1;
                        }
                    }
                    G[GL] = 0;
                }
#endif /* GW */
#else
                G[0] ^= ACC;
#endif
            }
        }
    }
    for (int b = 0; b < 64; b++)
        xb[b * nth + q] = X[b];
#if GW
    for (int j = 0; j < GW; j++)
#else
    for (int j = 0; j < GL; j++)
#endif
        gp[j * nth + q] = G[j];
}

/* カット値を数える（最初と最後）。cnt[32q + i] */
kernel void count_b(device const uint *col [[buffer(0)]],
                    constant uint *adjm [[buffer(8)]],
                    constant Params &P [[buffer(12)]],
                    device int *cnt [[buffer(14)]],
                    uint q [[thread_position_in_grid]])
{
    const uint nth = P.nth;
    /* 32 レプリカぶんの数をビットスライスの 16 桁の数で足す（辺の両端で 2 回数えるので最後に半分） */
    uint C[16];
    for (int b = 0; b < 16; b++)
        C[b] = 0;
    for (uint v = 0; v < N; v++) {
        uint wv = col[v * nth + q];
        for (int j = 0; j < D; j++) {
            uint e = col[adjm[v * D + j] + q] ^ wv;
            for (int b = 0; b < 16; b++) {
                uint cy = C[b] & e;
                C[b] ^= e;
                e = cy;
            }
        }
    }
    for (int i = 0; i < 32; i++) {
        int c = 0;
        for (int b = 0; b < 16; b++)
            c |= (int)((C[b] >> i) & 1) << b;
        cnt[q * 32 + i] = c / 2;
    }
}
