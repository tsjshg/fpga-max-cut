/*
 * pt_fast.c — pt.c と同じ規則のレプリカ交換法を、CPU で速く回す版（比較相手）
 *
 * pt.c と**ビット単位で同じ結果**を出す（同じ -R・-S・-w・-t・-e・--lag と .ladder なら、標準出力と
 * --init・--final の塗り分けが一致する）。規則は pt.c の冒頭を参照。
 *
 * 速くする工夫は sa_fast.c の既定のエンジン bsasg と同じ（docs/cpu-fast.md）:
 *   - 塗り分けを頂点ごとの語に（COL[v] のビット i = 組の i 番目のレプリカの色）。128 レプリカを 1 組に
 *   - xorshift64 の状態もビットスライス（X[b] のビット i = レプリカ i の状態のビット b）。1 回は語の XOR 155 回
 *     （xs_fused.h のアセンブリ）
 *   - 受け入れの比較（r < TH[ns]）は語のまま上の桁から
 *   - カット値は差 g = best − c の下 GL 桁を語の桁で持って足し込み、あふれたレプリカだけ 1 つずつ直す
 *
 * 焼きなましとの違いは、レプリカごとに温度（しきい値の行 TH[tidx]）が違うこと:
 *   - 1 組 = G = ⌊128 / M⌋ 個の系（M = 16 なら 8 系 × 16 レプリカ）。系は組をまたがない。余りは埋め草
 *   - しきい値を「ΔC < 0 の種類 q ごとに、桁 b の語」TB[q][b]（ビット i = TH[tidx_i][q] の桁 b）で持つ。
 *     桁 b でレプリカが使うしきい値のビットは、ns = q のレプリカに TB[q][b] を選んだもの。
 *     q が 2 つ（d = 3・4）なら TB[1][b] ^ (EQ[0] & (TB[0][b] ^ TB[1][b])) の 2 演算
 *   - 交換の判断（周回ごと・系ごと・組ごとに交換用の xorshift64 を 1 回）は回数が少ないのでスカラー。
 *     その時点のカット値 c = best − g を語の桁から取り出して使う。温度が変わったレプリカだけ
 *     TB の桁を差分で直す
 *
 * 時間は標準エラーへ。
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TARGETS 8
#define NONE UINT64_MAX
#define GOLDEN 0x9E3779B97F4A7C15ull
#define MMAX 64
#define W 128     /* 1 組のレプリカ数 */
#define BS_F 12   /* 比べ方で、まとめて比べる上の桁数（sa_fast と同じ） */
#define GL 6      /* 差 best − c の下の何桁を語の桁で持つか（sa_fast と同じ） */
#define BS_KMAX 4 /* ΔC < 0 の種類が 4 つ（d ≤ 8）まで */

typedef uint64_t VT __attribute__((vector_size(16)));
#define ANY(x) (((x)[0] | (x)[1]) != 0)
#define BIT(x, i) ((((const uint64_t *)&(x))[(i) / 64] >> ((i) % 64)) & 1)
#define FLIP(x, i) (((uint64_t *)&(x))[(i) / 64] ^= 1ull << ((i) % 64))

#include "xs_fused.h"

static uint64_t sm64(uint64_t *s)
{
    uint64_t z = (*s += GOLDEN);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline uint64_t xs64(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* ---- 入力（pt.c と同じ） ---- */

static int n, d, M, dmax;
static uint32_t *adj, *TH, *SW; /* TH: M×(d+1)、SW: (M−1)×dmax */
static uint64_t sweeps_total, exch_every = 1;
static int lag;
static int ntargets, quiet, nthreads = 1, R; /* R = 系の数 */
static uint8_t *colors0;                     /* 初期の塗り分け（--init で書き出す） */
static int32_t targets[MAX_TARGETS];
static uint64_t master_seed = 1;

typedef struct {
    uint64_t seed;
    int32_t c0, best, final;
    uint64_t best_t, hit_t[MAX_TARGETS];
    int tidx;
} result_t;
static result_t *res;
static uint8_t *colors;

static void *xalloc64(size_t size)
{
    void *p = aligned_alloc(64, (size + 63) / 64 * 64);
    if (!p) {
        perror("aligned_alloc");
        exit(1);
    }
    return p;
}

static FILE *xopen(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        exit(1);
    }
    return f;
}

static long long read_int(FILE *f, const char *path)
{
    int c;
    long long v;
    for (;;) {
        c = fgetc(f);
        if (c == '#') {
            while ((c = fgetc(f)) != '\n' && c != EOF)
                ;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        } else {
            break;
        }
    }
    if (c == EOF || (ungetc(c, f), fscanf(f, "%lld", &v) != 1)) {
        fprintf(stderr, "%s: 読めない\n", path);
        exit(1);
    }
    return v;
}

static void load_graph(const char *path)
{
    FILE *f = xopen(path);
    n = (int)read_int(f, path);
    d = (int)read_int(f, path);
    adj = malloc(sizeof(uint32_t) * n * d);
    for (int i = 0; i < n * d; i++)
        adj[i] = (uint32_t)read_int(f, path);
    fclose(f);
}

static void load_ladder(const char *path)
{
    FILE *f = xopen(path);
    if ((int)read_int(f, path) != d) {
        fprintf(stderr, "%s: d が合わない\n", path);
        exit(1);
    }
    M = (int)read_int(f, path);
    dmax = (int)read_int(f, path);
    if (M < 2 || M > MMAX) {
        fprintf(stderr, "M は 2〜%d\n", MMAX);
        exit(1);
    }
    TH = malloc(sizeof(uint32_t) * M * (d + 1));
    SW = malloc(sizeof(uint32_t) * (M - 1) * dmax);
    for (int i = 0; i < M * (d + 1); i++)
        TH[i] = (uint32_t)read_int(f, path);
    for (int i = 0; i < (M - 1) * dmax; i++)
        SW[i] = (uint32_t)read_int(f, path);
    fclose(f);
}

static int32_t cut_value(const uint8_t *s)
{
    int32_t c = 0;
    for (int v = 0; v < n; v++)
        for (int i = 0; i < d; i++)
            c += s[v] != s[adj[v * d + i]];
    return c / 2;
}

/* sa.c の init_replica と同じ */
static uint64_t init_replica(int p, uint8_t *s)
{
    uint64_t st = master_seed + ((uint64_t)p << 32) * GOLDEN;
    uint64_t seed;
    do
        seed = sm64(&st);
    while (seed == 0);
    uint64_t w = 0;
    for (int v = 0; v < n; v++) {
        if (v % 64 == 0)
            w = sm64(&st);
        s[v] = (w >> (v % 64)) & 1;
    }
    return seed;
}

/* ---- 改善の記録（sa_fast と同じ） ---- */

typedef struct {
    int32_t best;
    int nt;
    int32_t tgt;
    uint64_t best_t;
    uint64_t hit_t[MAX_TARGETS];
} track_t;

static void track_init(track_t *tr, int32_t c0)
{
    tr->best = c0;
    tr->best_t = NONE;
    tr->nt = 0;
    tr->tgt = ntargets ? targets[0] : INT32_MAX;
    for (int j = 0; j < MAX_TARGETS; j++)
        tr->hit_t[j] = NONE;
}

static __attribute__((noinline)) void track_hit(track_t *tr, int32_t c, uint64_t t)
{
    while (tr->nt < ntargets && c >= targets[tr->nt])
        tr->hit_t[tr->nt++] = t;
    tr->tgt = tr->nt < ntargets ? targets[tr->nt] : INT32_MAX;
}

static inline void track_update(track_t *tr, int32_t c, uint64_t t)
{
    int imp = c > tr->best;
    tr->best = imp ? c : tr->best;
    tr->best_t = imp ? t : tr->best_t;
    if (__builtin_expect(imp & (c >= tr->tgt), 0))
        track_hit(tr, c, t);
}

/* ---- 交換（pt.c と同じ。系の中の番号 j で持つ） ---- */

static uint64_t decide(int q, const int *perm, const int32_t *c, uint64_t *ex)
{
    uint64_t mask = 0;
    for (int k = q & 1; k + 1 < M; k += 2) {
        uint32_t r = (uint32_t)(xs64(ex) >> 32);
        int32_t D = c[perm[k]] - c[perm[k + 1]];
        if (D <= 0 || r < SW[k * dmax + (D < dmax ? D : dmax) - 1])
            mask |= 1ull << k;
    }
    return mask;
}

static void apply(uint64_t mask, int *perm, int *tidx)
{
    for (int k = 0; k + 1 < M; k++)
        if (mask >> k & 1) {
            int a = perm[k], b = perm[k + 1];
            perm[k] = b;
            perm[k + 1] = a;
            tidx[b] = k;
            tidx[a] = k + 1;
        }
}

/* ---- 1 組（G 系 = G·M レプリカ）を最後まで回す ---- */

static inline __attribute__((always_inline)) void xs_step(VT *restrict w, const VT *restrict x)
{
#if defined(__aarch64__)
    XS_A64(w, x);
#elif defined(__arm__) && defined(__ARM_NEON)
    XS_A32(w, x);                                /* レプリカ 0..63 */
    XS_A32((char *)w + 8, (const char *)x + 8); /* レプリカ 64..127 */
#else
    XS_FUSED(w, x);
#endif
}

/* tidx が変わったレプリカ i のしきい値の語を差分で直す */
static void tb_move(VT (*TB)[32], int K, int i, int from, int to)
{
    for (int q = 0; q < K; q++) {
        uint32_t df = TH[from * (d + 1) + q] ^ TH[to * (d + 1) + q];
        while (df) {
            FLIP(TB[q][__builtin_ctz(df)], i);
            df &= df - 1;
        }
    }
}

static inline __attribute__((always_inline)) void run_group(int gi, const int D)
{
    const int K = (D + 1) / 2;                           /* ns < K なら ΔC < 0（しきい値を引く） */
    const int NB = D < 2 ? 1 : D < 4 ? 2 : D < 8 ? 3 : 4; /* ns（0..D）のビット数 */
    const int DB = NB + 2;                               /* δ = D − 2·ns の桁数。これより上は符号の桁 */
    const int G = W / M;
    const int L0 = gi * G, ng = R - L0 < G ? R - L0 : G; /* この組の系 */
    const int p0 = L0 * M, nv = ng * M;                  /* 本物のレプリカ（残りは埋め草） */

    VT *COL = xalloc64(sizeof(VT) * n);
    VT(*XB)[64] = xalloc64(sizeof(VT) * 128); /* XB[0]・XB[1] を交互に */
    VT(*TB)[32] = xalloc64(sizeof(VT) * 32 * BS_KMAX);
    VT *X = XB[0];
    memset(COL, 0, sizeof(VT) * n);
    memset(XB, 0, sizeof(VT) * 128);
    memset(TB, 0, sizeof(VT) * 32 * BS_KMAX);
    track_t tr[W];
    int32_t c[W]; /* 交換の時点のカット値 */
    int tidx[W];
    for (int i = 0; i < W; i++) {
        uint64_t x = 1; /* 埋め草（全頂点 0 色、温度 0、結果は捨てる） */
        c[i] = 0;
        tidx[i] = 0;
        if (i < nv) {
            int p = p0 + i;
            uint8_t *s = colors0 + (size_t)p * n;
            x = res[p].seed = init_replica(p, s);
            for (int v = 0; v < n; v++)
                if (s[v])
                    FLIP(COL[v], i);
            c[i] = res[p].c0 = cut_value(s);
            tidx[i] = i % M;
        }
        for (int b = 0; b < 64; b++)
            if ((x >> b) & 1)
                FLIP(X[b], i);
        track_init(&tr[i], c[i]);
        for (int q = 0; q < K; q++)
            for (int b = 0; b < 32; b++)
                if ((TH[tidx[i] * (d + 1) + q] >> b) & 1)
                    FLIP(TB[q][b], i);
    }
    /* 系ごとの交換の状態 */
    int perm[W];
    uint64_t ex[W / 2], pending[W / 2];
    for (int s = 0; s < ng; s++) {
        int L = L0 + s;
        for (int j = 0; j < M; j++)
            perm[s * M + j] = j;
        uint64_t st = master_seed + ((uint64_t)(0x80000000u + (uint32_t)L) << 32) * GOLDEN;
        do
            ex[s] = sm64(&st);
        while (ex[s] == 0);
        pending[s] = 0;
    }
    int q_ex = 0;

    /* どの温度のしきい値でも 0 の上位 z 桁は「r のその桁が 0 か」を見るだけでよい */
    uint32_t thor = 0;
    for (int k = 0; k < M; k++)
        for (int q = 0; q < K; q++)
            thor |= TH[k * (d + 1) + q];
    const int z = __builtin_clz(thor | 1);
    const int stop0 = 32 - (z > BS_F ? z : BS_F);

    /* g = best − c ≥ 0 の下 GL 桁 GP（語の桁）と上の桁 H（sa_fast の TRK_GAP と同じ） */
    VT GP[GL + 1];
    for (int j = 0; j <= GL; j++)
        GP[j] = (VT){0};
    int32_t H[W];
    memset(H, 0, sizeof H);

    uint64_t t = 0;
    for (uint64_t sw = 0; sw < sweeps_total; sw++) {
        if (K == 2)
            for (int b = 0; b < 32; b++)
                TB[2][b] = TB[0][b] ^ TB[1][b]; /* 下の選び方用（温度は周回の中で変わらない） */
        for (int v = 0; v < n; v++, t++) {
            {
                VT *Xn = X == XB[0] ? XB[1] : XB[0];
                xs_step(Xn, X);
                X = Xn;
            }
            const uint32_t *nb = adj + v * D;
            VT own = COL[v];
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
            VT NEG = {0};
            for (int q = 0; q < K; q++)
                NEG |= EQ[q];

            /* LT = (r < TH[tidx][ns])。ΔC < 0 のレプリカについてだけ意味がある */
            VT LT = {0};
            {
                VT eq = NEG;
                int b = 31;
                for (; b >= 32 - z; b--)
                    eq &= ~X[32 + b];
                int stop = stop0;
                for (;;) {
                    for (; b >= stop; b--) {
                        VT r = X[32 + b];
                        VT tb;
                        if (K == 2) {
                            tb = TB[1][b] ^ (EQ[0] & TB[2][b]);
                        } else {
                            tb = EQ[0] & TB[0][b];
                            for (int q = 1; q < K; q++)
                                tb |= EQ[q] & TB[q][b];
                        }
                        LT |= eq & tb & ~r;
                        eq &= ~(r ^ tb);
                    }
                    if (__builtin_expect(!ANY(eq), 1) || b < 0)
                        break;
                    stop = b;
                }
            }
            VT ACC = ~NEG | LT;
            COL[v] = own ^ ACC;

            /* g += δ（sa_fast の TRK_GAP と同じ） */
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
                VT WR = GP[GL];
                if (__builtin_expect(ANY(WR), 0)) {
                    for (int l = 0; l < 2; l++) {
                        uint64_t mk = WR[l];
                        while (mk) {
                            int i = l * 64 + __builtin_ctzll(mk);
                            mk &= mk - 1;
                            int32_t lo = 0;
                            for (int j = 0; j < GL; j++)
                                lo |= (int32_t)BIT(GP[j], i) << j;
                            if (BIT(DL[DB - 1], i)) {
                                if (H[i] > 0) {
                                    H[i]--;
                                } else {
                                    track_update(&tr[i], tr[i].best - (lo - (1 << GL)), t);
                                    for (int j = 0; j < GL; j++)
                                        GP[j][l] &= ~(1ull << (i % 64));
                                }
                            } else {
                                H[i]++;
                            }
                        }
                    }
                    GP[GL] = (VT){0};
                }
            }
        }

        if ((sw + 1) % exch_every == 0) {
            /* いまのカット値 c = best − g を取り出し、系ごとに交換する */
            for (int i = 0; i < nv; i++) {
                int32_t lo = 0;
                for (int j = 0; j < GL; j++)
                    lo |= (int32_t)BIT(GP[j], i) << j;
                c[i] = tr[i].best - ((H[i] << GL) + lo);
            }
            for (int s = 0; s < ng; s++) {
                int *ps = perm + s * M, *ts = tidx + s * M, old[MMAX];
                memcpy(old, ts, sizeof(int) * M);
                if (lag) {
                    apply(pending[s], ps, ts);
                    pending[s] = decide(q_ex, ps, c + s * M, &ex[s]);
                } else {
                    apply(decide(q_ex, ps, c + s * M, &ex[s]), ps, ts);
                }
                for (int j = 0; j < M; j++)
                    if (ts[j] != old[j])
                        tb_move(TB, K, s * M + j, old[j], ts[j]);
            }
            q_ex++;
        }
    }

    for (int i = 0; i < nv; i++) {
        int p = p0 + i;
        uint8_t *s = colors + (size_t)p * n;
        for (int v = 0; v < n; v++)
            s[v] = BIT(COL[v], i);
        int32_t lo = 0;
        for (int j = 0; j < GL; j++)
            lo |= (int32_t)BIT(GP[j], i) << j;
        result_t *r = &res[p];
        r->best = tr[i].best;
        r->best_t = tr[i].best_t;
        r->final = tr[i].best - ((H[i] << GL) + lo);
        memcpy(r->hit_t, tr[i].hit_t, sizeof r->hit_t);
        r->tidx = tidx[i];
        if (cut_value(s) != r->final) {
            fprintf(stderr, "系 %d のレプリカ %d: カット値が合わない\n", p / M, p % M);
            exit(1);
        }
    }
    free(COL);
    free(XB);
    free(TB);
}

static void run_d4(int gi) { run_group(gi, 4); }
static void run_d3(int gi) { run_group(gi, 3); }
static void run_dn(int gi) { run_group(gi, d); }

/* ---- スレッド（組を早い者勝ちで取る。結果は割り当てによらない） ---- */

static void (*job)(int);
static int njobs, next_job;

static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        int j = __atomic_fetch_add(&next_job, 1, __ATOMIC_RELAXED);
        if (j >= njobs)
            break;
        job(j);
    }
    return NULL;
}

static void print_t(uint64_t t)
{
    if (t == NONE)
        printf(" -1");
    else
        printf(" %" PRIu64, t);
}

static void dump_colors(const char *path, const uint8_t *buf)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(1);
    }
    for (int p = 0; p < R * M; p++) {
        for (int v = 0; v < n; v++)
            fputc('0' + buf[(size_t)p * n + v], f);
        fputc('\n', f);
    }
    fclose(f);
}

static void usage(void)
{
    fprintf(stderr, "使い方: pt_fast -g graph.adj -l ladder.ladder -w 周回数 [-R 系の数] [-S 主種] [-t 目標]...\n"
                    "         [-e 交換の間隔（周回）] [--lag 0|1] [-j スレッド数] [--init 書き出し先] [--final 書き出し先] [-q]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *gpath = NULL, *lpath = NULL, *init_path = NULL, *final_path = NULL;
    R = 8;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "-q")) {
            quiet = 1;
            continue;
        }
        if (!v)
            usage();
        i++;
        if (!strcmp(a, "-g"))
            gpath = v;
        else if (!strcmp(a, "-l"))
            lpath = v;
        else if (!strcmp(a, "-w"))
            sweeps_total = strtoull(v, NULL, 0);
        else if (!strcmp(a, "-R"))
            R = atoi(v);
        else if (!strcmp(a, "-S"))
            master_seed = strtoull(v, NULL, 0);
        else if (!strcmp(a, "-t") && ntargets < MAX_TARGETS)
            targets[ntargets++] = atoi(v);
        else if (!strcmp(a, "-e"))
            exch_every = strtoull(v, NULL, 0);
        else if (!strcmp(a, "--lag"))
            lag = atoi(v);
        else if (!strcmp(a, "-j"))
            nthreads = atoi(v);
        else if (!strcmp(a, "--init"))
            init_path = v;
        else if (!strcmp(a, "--final"))
            final_path = v;
        else
            usage();
    }
    if (!gpath || !lpath || !sweeps_total || R < 1 || exch_every < 1 || nthreads < 1)
        usage();
    load_graph(gpath);
    load_ladder(lpath);
    if ((d + 1) / 2 > BS_KMAX) {
        fprintf(stderr, "pt_fast は d ≤ %d だけ\n", 2 * BS_KMAX);
        return 2;
    }
    res = calloc((size_t)R * M, sizeof *res);
    colors = malloc((size_t)R * M * n);
    colors0 = malloc((size_t)R * M * n);
    job = d == 4 ? run_d4 : d == 3 ? run_d3 : run_dn;
    int G = W / M;
    njobs = (R + G - 1) / G;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (nthreads == 1) {
        worker(NULL);
    } else {
        pthread_t *th = malloc(sizeof(pthread_t) * nthreads);
        for (int i = 0; i < nthreads; i++)
            pthread_create(&th[i], NULL, worker, NULL);
        for (int i = 0; i < nthreads; i++)
            pthread_join(th[i], NULL);
        free(th);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double sec = (t1.tv_sec - t0.tv_sec) + 1e-9 * (t1.tv_nsec - t0.tv_nsec);

    printf("# n=%d d=%d systems=%d M=%d seed=%" PRIu64 " sweeps=%" PRIu64 " exch=%" PRIu64 " lag=%d\n", n, d, R,
           M, master_seed, sweeps_total, exch_every, lag);
    if (!quiet)
        for (int p = 0; p < R * M; p++) {
            result_t *r = &res[p];
            printf("%d %016" PRIx64 " %d %d", p, r->seed, r->c0, r->best);
            print_t(r->best_t);
            printf(" %d", r->final);
            for (int j = 0; j < ntargets; j++)
                print_t(r->hit_t[j]);
            printf(" %d\n", r->tidx);
        }
    long long sum = 0;
    int hits[MAX_TARGETS] = {0};
    for (int L = 0; L < R; L++) {
        int32_t b = 0;
        for (int j = 0; j < M; j++)
            if (res[L * M + j].best > b)
                b = res[L * M + j].best;
        sum += b;
        for (int k = 0; k < ntargets; k++) {
            uint64_t h = NONE;
            for (int j = 0; j < M; j++)
                if (res[L * M + j].hit_t[k] < h)
                    h = res[L * M + j].hit_t[k];
            hits[k] += h != NONE;
        }
    }
    printf("summary system best: mean=%.3f\n", (double)sum / R);
    for (int k = 0; k < ntargets; k++)
        printf("summary target %d: %d/%d systems\n", targets[k], hits[k], R);
    fprintf(stderr, "time %.3f s, %.3g trials/s (threads=%d)\n", sec, (double)R * M * n * sweeps_total / sec,
            nthreads);
    if (init_path)
        dump_colors(init_path, colors0);
    if (final_path)
        dump_colors(final_path, colors);
    return 0;
}
