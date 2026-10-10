/*
 * sa_fast.c — sa.c と同じ規則の焼きなましを、CPU で速く回す版（比較相手）
 *
 * sa.c と**ビット単位で同じ結果**を出す（同じ -R・-S・-t・スケジュールなら標準出力と
 * --init・--final の塗り分けが一致する）。規則は sa.c の冒頭を参照。ここで扱うのは
 * 頂点を順に回す・xorshift64 の規則だけ（--order rand・--rng sm64 は無い）。
 *
 * 速くする工夫は段階ごとにエンジンとして残してあり、-e で選べる（どれも結果は同じ）:
 *
 *   ref      sa.c と同じ書き方（レプリカごと・受け入れを分岐で・色は 1 バイト）
 *   nobr     受け入れを分岐でなく算術で（best の更新も条件つき代入）
 *   inter4   nobr を 4 レプリカまとめて頂点ごとに交互に回す（依存の連鎖を隠す）
 *   word     色を頂点ごとの語（ビット i = レプリカ i、回路と同じ形）にし、64 レプリカの
 *            ΔC と受け入れをビット演算でまとめて作る。乱数はレプリカごとにスカラー
 *   neon     word の xorshift64 を NEON の 64 bit × 2 レーンで回す
 *   bsin     乱数の状態もビットスライスにする（語 X[b] のビット i = レプリカ i の状態のビット b）。
 *            xorshift64 は 3 段のシフトを語の上で順に（語の XOR 155 回）。r < TH も語のまま
 *            上の桁から比べる。128 レプリカ
 *   bsfu     bsin の 3 段を1段にまとめる（新しい状態の各ビットを古い状態の 2〜8 ビットの XOR で直接作る）
 *   bs64/bs128/bs256
 *            bsfu の c の足し込みもビットスライスにする（差 best − c を語の桁で持ち、分岐なしで足す）。
 *            語の幅（= 1 組のレプリカ数）が 64・128・256
 *
 * 時間は標準エラーへ。Linux では --pmu で CPU のカウンタ（サイクル・命令・分岐の予測外れ）も出す。
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define MAX_TARGETS 8
#define NONE UINT64_MAX
#define GOLDEN 0x9E3779B97F4A7C15ull
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)

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

/* ---- 入力（sa.c と同じ） ---- */

static int n, d;
static uint32_t *adj;    /* n*d */
static int S;            /* 温度の段数 */
static uint32_t *sweeps; /* S */
static uint32_t *th;     /* S*(d+1) */
static uint64_t total_sweeps;

static int ntargets;
static int32_t targets[MAX_TARGETS];
static uint64_t master_seed = 1;

/* ---- 結果 ---- */

typedef struct {
    uint64_t seed;
    int32_t c0, best, final;
    uint64_t best_t;
    uint64_t hit_t[MAX_TARGETS];
} result_t;

static int R;
static result_t *res;
static uint8_t *colors0, *colors; /* R*n（初期と最後の塗り分け） */

/* 64 バイト境界の確保（aligned_alloc は大きさが境界の倍数でないと失敗する） */
static void *xalloc64(size_t size)
{
    void *p = aligned_alloc(64, (size + 63) / 64 * 64);
    if (!p) {
        perror("aligned_alloc");
        exit(1);
    }
    return p;
}

static FILE *xopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
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
    if (c == EOF) {
        fprintf(stderr, "%s: 途中で終わっている\n", path);
        exit(1);
    }
    ungetc(c, f);
    if (fscanf(f, "%lld", &v) != 1) {
        fprintf(stderr, "%s: 数でないものがある\n", path);
        exit(1);
    }
    return v;
}

static void load_graph(const char *path)
{
    FILE *f = xopen(path, "r");
    n = (int)read_int(f, path);
    d = (int)read_int(f, path);
    adj = malloc(sizeof(uint32_t) * n * d);
    for (int i = 0; i < n * d; i++) {
        long long u = read_int(f, path);
        if (u < 0 || u >= n || u == i / d) {
            fprintf(stderr, "%s: 頂点 %d の隣 %lld が範囲外か自己ループ\n", path, i / d, u);
            exit(1);
        }
        adj[i] = (uint32_t)u;
    }
    fclose(f);
    for (int v = 0; v < n; v++)
        for (int i = 0; i < d; i++) {
            uint32_t u = adj[v * d + i];
            int found = 0;
            for (int j = 0; j < d; j++)
                found += adj[u * d + j] == (uint32_t)v;
            if (found != 1) {
                fprintf(stderr, "%s: 辺 %d-%u が対称でないか多重辺\n", path, v, u);
                exit(1);
            }
        }
}

static void load_sched(const char *path)
{
    FILE *f = xopen(path, "r");
    int sd = (int)read_int(f, path);
    if (sd != d) {
        fprintf(stderr, "%s: d=%d だがグラフは d=%d\n", path, sd, d);
        exit(1);
    }
    S = (int)read_int(f, path);
    sweeps = malloc(sizeof(uint32_t) * S);
    th = malloc(sizeof(uint32_t) * S * (d + 1));
    total_sweeps = 0;
    for (int k = 0; k < S; k++) {
        sweeps[k] = (uint32_t)read_int(f, path);
        total_sweeps += sweeps[k];
        for (int j = 0; j <= d; j++)
            th[k * (d + 1) + j] = (uint32_t)read_int(f, path);
    }
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

/* レプリカ p の種と初期の塗り分け（sa.c と同じ） */
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
    res[p].seed = seed;
    memcpy(colors0 + (size_t)p * n, s, n);
    return seed;
}

static void check_final(int p)
{
    const uint8_t *s = colors + (size_t)p * n;
    if (cut_value(s) != res[p].final) {
        fprintf(stderr, "レプリカ %d: 足し込んだカット値 %d と数え直した値 %d が違う\n", p,
                res[p].final, cut_value(s));
        exit(1);
    }
}

/* 改善の記録。sa.c では「受け入れ かつ c > best」のときだけ目標を見る。
 * 目標に届くのは改善のときだけなので、tgt（次の目標、無ければ INT32_MAX）と比べる枝はまれにしか通らない */
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

static void track_store(const track_t *tr, int p, int32_t c0, int32_t c)
{
    res[p].c0 = c0;
    res[p].best = tr->best;
    res[p].best_t = tr->best_t;
    res[p].final = c;
    memcpy(res[p].hit_t, tr->hit_t, sizeof tr->hit_t);
    check_final(p);
}

/* ================= エンジン ================= */

/* ref: sa.c の run_replica と同じ書き方 */
static void eng_ref(int p)
{
    uint8_t *s = colors + (size_t)p * n;
    uint64_t x = init_replica(p, s);
    int32_t c = cut_value(s), c0 = c;
    int32_t best = c;
    uint64_t best_t = NONE;
    uint64_t hit_t[MAX_TARGETS];
    int next_target = 0;
    for (int j = 0; j < MAX_TARGETS; j++)
        hit_t[j] = NONE;
    uint64_t t = 0;
    for (int k = 0; k < S; k++) {
        const uint32_t *T = th + k * (d + 1);
        for (uint32_t m = 0; m < sweeps[k]; m++)
            for (int v = 0; v < n; v++, t++) {
                uint32_t r = (uint32_t)(xs64(&x) >> 32);
                const uint32_t *nb = adj + v * d;
                uint8_t sv = s[v];
                int ns = 0;
                for (int j = 0; j < d; j++)
                    ns += s[nb[j]] == sv;
                int dc = 2 * ns - d;
                if (dc >= 0 || r < T[ns]) {
                    s[v] = sv ^ 1;
                    c += dc;
                    if (c > best) {
                        best = c;
                        best_t = t;
                        while (next_target < ntargets && c >= targets[next_target])
                            hit_t[next_target++] = t;
                    }
                }
            }
    }
    res[p].c0 = c0;
    res[p].best = best;
    res[p].best_t = best_t;
    res[p].final = c;
    memcpy(res[p].hit_t, hit_t, sizeof hit_t);
    check_final(p);
}

/* nobr: 受け入れを算術で。acc は 0 か 1 */
static void eng_nobr(int p)
{
    uint8_t *s = colors + (size_t)p * n;
    uint64_t x = init_replica(p, s);
    int32_t c = cut_value(s), c0 = c;
    track_t tr;
    track_init(&tr, c);
    uint64_t t = 0;
    for (int k = 0; k < S; k++) {
        const uint32_t *T = th + k * (d + 1);
        for (uint32_t m = 0; m < sweeps[k]; m++)
            for (int v = 0; v < n; v++, t++) {
                uint32_t r = (uint32_t)(xs64(&x) >> 32);
                const uint32_t *nb = adj + v * d;
                uint8_t sv = s[v];
                int ns = 0;
                for (int j = 0; j < d; j++)
                    ns += s[nb[j]] == sv;
                int dc = 2 * ns - d;
                uint32_t acc = (uint32_t)(dc >= 0) | (uint32_t)(r < T[ns]);
                s[v] = sv ^ (uint8_t)acc;
                c += dc & -(int32_t)acc;
                track_update(&tr, c, t);
            }
    }
    track_store(&tr, p, c0, c);
}

/* inter4: nobr を 4 レプリカ（p0..p0+3）まとめ、頂点ごとに交互に回す */
#define G4 4
static void eng_inter4(int gi)
{
    int p0 = gi * G4, nv = R - p0 < G4 ? R - p0 : G4;
    uint8_t *s[G4];
    uint8_t *dummy = NULL;
    uint64_t x[G4];
    int32_t c[G4], c0[G4];
    track_t tr[G4];
    for (int g = 0; g < G4; g++) {
        if (g < nv) {
            s[g] = colors + (size_t)(p0 + g) * n;
            x[g] = init_replica(p0 + g, s[g]);
        } else {
            if (!dummy)
                dummy = calloc(n, 1);
            s[g] = dummy;
            x[g] = 1;
        }
        c[g] = c0[g] = cut_value(s[g]);
        track_init(&tr[g], c[g]);
    }
    uint64_t t = 0;
    for (int k = 0; k < S; k++) {
        const uint32_t *T = th + k * (d + 1);
        for (uint32_t m = 0; m < sweeps[k]; m++)
            for (int v = 0; v < n; v++, t++) {
                const uint32_t *nb = adj + v * d;
                for (int g = 0; g < G4; g++) {
                    uint32_t r = (uint32_t)(xs64(&x[g]) >> 32);
                    uint8_t sv = s[g][v];
                    int ns = 0;
                    for (int j = 0; j < d; j++)
                        ns += s[g][nb[j]] == sv;
                    int dc = 2 * ns - d;
                    uint32_t acc = (uint32_t)(dc >= 0) | (uint32_t)(r < T[ns]);
                    s[g][v] = sv ^ (uint8_t)acc;
                    c[g] += dc & -(int32_t)acc;
                    track_update(&tr[g], c[g], t);
                }
            }
    }
    for (int g = 0; g < nv; g++)
        track_store(&tr[g], p0 + g, c0[g], c[g]);
    free(dummy);
}

/* ---- 語のエンジンの共通部分（1 組 = W レプリカ） ---- */

typedef struct {
    int p0, nv;  /* 組の最初のレプリカと、本物のレプリカの数（残りは埋め草） */
    uint8_t *s0; /* nv*n: 初期の塗り分け（colors0 の中） */
    uint8_t *s;  /* nv*n: 最後の塗り分け（colors の中） */
    uint64_t x[256];
    int32_t c[256], c0[256];
    track_t tr[256];
} grp_t;

static grp_t *grp_new(int gi, int w)
{
    grp_t *g = calloc(1, sizeof *g);
    g->p0 = gi * w;
    g->nv = R - g->p0 < w ? R - g->p0 : w;
    g->s0 = colors0 + (size_t)g->p0 * n;
    g->s = colors + (size_t)g->p0 * n;
    for (int i = 0; i < w; i++) {
        if (i < g->nv) {
            uint8_t *s = g->s0 + (size_t)i * n;
            g->x[i] = init_replica(g->p0 + i, s);
            g->c[i] = cut_value(s);
        } else {
            g->x[i] = 1; /* 埋め草（全頂点 0 色、結果は捨てる） */
            g->c[i] = 0;
        }
        g->c0[i] = g->c[i];
        track_init(&g->tr[i], g->c[i]);
    }
    return g;
}

static inline void grp_add(grp_t *g, int i, int dc, uint64_t t)
{
    int32_t c = g->c[i] + dc;
    g->c[i] = c;
    track_update(&g->tr[i], c, t);
}

static void grp_done(grp_t *g)
{
    for (int i = 0; i < g->nv; i++)
        track_store(&g->tr[i], g->p0 + i, g->c0[i], g->c[i]);
    free(g);
}

#define RNG_SCALAR 0
#define RNG_NEON 1
#define RNG_BSIN 2
#define RNG_BSFU 3
#define RNG_BSAS 4
#define TRK_EV 0
#define TRK_GAP 1
#ifndef BS_F
#define BS_F 12 /* ビットスライスの比べ方で、まとめて比べる上の桁数 */
#endif
#ifndef GL
#define GL 6 /* TRK_GAP で差 best − c の下の何桁をビットスライスで持つか */
#endif
#define BS_KMAX 4 /* ビットスライスの比べ方は ΔC < 0 の種類が 4 つ（d ≤ 8）まで */
#include "xs_fused.h"

#define ENG eng_word_d4
#define VW 1
#define RNGK RNG_SCALAR
#define TRK TRK_EV
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_word_dn
#define VW 1
#define RNGK RNG_SCALAR
#define TRK TRK_EV
#define DC 0
#include "sa_fast_grp.h"
#ifdef __ARM_NEON
#define ENG eng_neon_d4
#define VW 1
#define RNGK RNG_NEON
#define TRK TRK_EV
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_neon_dn
#define VW 1
#define RNGK RNG_NEON
#define TRK TRK_EV
#define DC 0
#include "sa_fast_grp.h"
#endif
#define ENG eng_bsin_d4
#define VW 2
#define RNGK RNG_BSIN
#define TRK TRK_EV
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bsfu_d4
#define VW 2
#define RNGK RNG_BSFU
#define TRK TRK_EV
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bsas_d4
#define VW 2
#define RNGK RNG_BSAS
#define TRK TRK_EV
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bsasg_d4
#define VW 2
#define RNGK RNG_BSAS
#define TRK TRK_GAP
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bsas_dn
#define VW 2
#define RNGK RNG_BSAS
#define TRK TRK_EV
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bsasg_dn
#define VW 2
#define RNGK RNG_BSAS
#define TRK TRK_GAP
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bsasg_d3
#define VW 2
#define RNGK RNG_BSAS
#define TRK TRK_GAP
#define DC 3
#include "sa_fast_grp.h"
#define ENG eng_bs64_d4
#define VW 1
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bs256_d4
#define VW 4
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bsin_dn
#define VW 2
#define RNGK RNG_BSIN
#define TRK TRK_EV
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bsfu_dn
#define VW 2
#define RNGK RNG_BSFU
#define TRK TRK_EV
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bs64_dn
#define VW 1
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bs256_dn
#define VW 4
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 0
#include "sa_fast_grp.h"
#define ENG eng_bs128_d4
#define VW 2
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 4
#include "sa_fast_grp.h"
#define ENG eng_bs128_dn
#define VW 2
#define RNGK RNG_BSFU
#define TRK TRK_GAP
#define DC 0
#include "sa_fast_grp.h"

typedef struct {
    const char *name;
    int unit;                 /* 1 つの仕事が受け持つレプリカの数 */
    int bs;                   /* ビットスライスの比べ方を使う（d ≤ 8 が要る） */
    void (*f4)(int), (*f3)(int), (*fn)(int); /* d = 4・d = 3・その他 */
} engine_t;

static const engine_t engines[] = {
    {"ref", 1, 0, eng_ref, eng_ref, eng_ref},
    {"nobr", 1, 0, eng_nobr, eng_nobr, eng_nobr},
    {"inter4", G4, 0, eng_inter4, eng_inter4, eng_inter4},
    {"word", 64, 0, eng_word_d4, eng_word_dn, eng_word_dn},
#ifdef __ARM_NEON
    {"neon", 64, 0, eng_neon_d4, eng_neon_dn, eng_neon_dn},
#endif
    {"bsin", 128, 1, eng_bsin_d4, eng_bsin_dn, eng_bsin_dn},
    {"bsfu", 128, 1, eng_bsfu_d4, eng_bsfu_dn, eng_bsfu_dn},
    {"bsas", 128, 1, eng_bsas_d4, eng_bsas_dn, eng_bsas_dn},
    {"bsasg", 128, 1, eng_bsasg_d4, eng_bsasg_d3, eng_bsasg_dn},
    {"bs64", 64, 1, eng_bs64_d4, eng_bs64_dn, eng_bs64_dn},
    {"bs128", 128, 1, eng_bs128_d4, eng_bs128_dn, eng_bs128_dn},
    {"bs256", 256, 1, eng_bs256_d4, eng_bs256_dn, eng_bs256_dn},
};
#define DEFAULT_ENGINE "bsasg"

/* ---- スレッド（仕事を早い者勝ちで取る。結果は割り当てによらない） ---- */

static void (*job)(int);
static int njobs;
static int next_job;
static int nthreads = 1;

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

/* ---- CPU のカウンタ（Linux だけ） ---- */

#ifdef __linux__
static const struct {
    const char *name;
    uint32_t type;
    uint64_t config;
} pmu_ev[] = {
    {"cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
    {"instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
    {"branches", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
    {"branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
    {"l1d-refill", PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) |
         (PERF_COUNT_HW_CACHE_RESULT_MISS << 16)},
};
#define NPMU (int)(sizeof pmu_ev / sizeof pmu_ev[0])
static int pmu_fd[NPMU];

static void pmu_start(void)
{
    for (int i = 0; i < NPMU; i++) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.size = sizeof a;
        a.type = pmu_ev[i].type;
        a.config = pmu_ev[i].config;
        a.disabled = 1;
        a.inherit = 1; /* 後で作るスレッドの分も足す */
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        pmu_fd[i] = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        if (pmu_fd[i] < 0) {
            /* Cortex-A9 の PMU はカーネルとユーザを分けて数えられない。分けずに数える
             * （perf_event_paranoid が 2 なら root が要る。走っている間のカーネルの分も入る） */
            a.exclude_kernel = 0;
            a.exclude_hv = 0;
            pmu_fd[i] = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        }
        if (pmu_fd[i] < 0)
            perror(pmu_ev[i].name);
        else
            ioctl(pmu_fd[i], PERF_EVENT_IOC_ENABLE, 0);
    }
}

static void pmu_stop(double trials)
{
    for (int i = 0; i < NPMU; i++) {
        if (pmu_fd[i] < 0)
            continue;
        ioctl(pmu_fd[i], PERF_EVENT_IOC_DISABLE, 0);
        uint64_t v = 0;
        if (read(pmu_fd[i], &v, sizeof v) != sizeof v)
            continue;
        fprintf(stderr, "pmu %-14s %15" PRIu64 "  %8.3f /試行\n", pmu_ev[i].name, v, v / trials);
        close(pmu_fd[i]);
    }
}
#endif

static void dump_colors(const char *path, const uint8_t *buf)
{
    FILE *f = xopen(path, "w");
    for (int p = 0; p < R; p++) {
        for (int v = 0; v < n; v++)
            fputc('0' + buf[(size_t)p * n + v], f);
        fputc('\n', f);
    }
    fclose(f);
}

static void print_t(uint64_t t)
{
    if (t == NONE)
        printf(" -1");
    else
        printf(" %" PRIu64, t);
}

static void usage(void)
{
    fprintf(stderr,
            "使い方: sa_fast -g graph.adj -s sched.sched [-R レプリカ数] [-S 主種] [-t 目標]...\n"
            "          [-j スレッド数] [-e エンジン] [--init 書き出し先] [--final 書き出し先] [-q]"
#ifdef __linux__
            " [--pmu]"
#endif
            "\n  エンジン:");
    for (size_t i = 0; i < sizeof engines / sizeof engines[0]; i++)
        fprintf(stderr, " %s", engines[i].name);
    fprintf(stderr, "（既定 %s）\n", DEFAULT_ENGINE);
    exit(2);
}

int main(int argc, char **argv)
{
    const char *gpath = NULL, *spath = NULL, *init_path = NULL, *final_path = NULL;
    const char *ename = DEFAULT_ENGINE;
    int quiet = 0, pmu = 0;
    R = 16;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED()       \
    do {             \
        if (!v)      \
            usage(); \
        i++;         \
    } while (0)
        if (!strcmp(a, "-g")) {
            NEED();
            gpath = v;
        } else if (!strcmp(a, "-s")) {
            NEED();
            spath = v;
        } else if (!strcmp(a, "-R")) {
            NEED();
            R = atoi(v);
        } else if (!strcmp(a, "-S")) {
            NEED();
            master_seed = strtoull(v, NULL, 0);
        } else if (!strcmp(a, "-t")) {
            NEED();
            if (ntargets == MAX_TARGETS)
                usage();
            targets[ntargets++] = atoi(v);
        } else if (!strcmp(a, "-j")) {
            NEED();
            nthreads = atoi(v);
        } else if (!strcmp(a, "-e")) {
            NEED();
            ename = v;
        } else if (!strcmp(a, "--init")) {
            NEED();
            init_path = v;
        } else if (!strcmp(a, "--final")) {
            NEED();
            final_path = v;
        } else if (!strcmp(a, "-q")) {
            quiet = 1;
        } else if (!strcmp(a, "--pmu")) {
            pmu = 1;
        } else {
            usage();
        }
    }
    if (!gpath || !spath || R < 1 || nthreads < 1)
        usage();
    for (int j = 1; j < ntargets; j++)
        if (targets[j] <= targets[j - 1]) {
            fprintf(stderr, "-t は小さい順に並べる\n");
            return 2;
        }
    const engine_t *eng = NULL;
    for (size_t i = 0; i < sizeof engines / sizeof engines[0]; i++)
        if (!strcmp(engines[i].name, ename))
            eng = &engines[i];
    if (!eng)
        usage();

    load_graph(gpath);
    load_sched(spath);
    if (eng->bs && (d + 1) / 2 > BS_KMAX) {
        fprintf(stderr, "%s は d ≤ %d だけ\n", eng->name, 2 * BS_KMAX);
        return 2;
    }
    res = calloc(R, sizeof *res);
    colors0 = malloc((size_t)R * n);
    colors = malloc((size_t)R * n);
    job = d == 4 ? eng->f4 : d == 3 ? eng->f3 : eng->fn;
    njobs = (R + eng->unit - 1) / eng->unit;

    double trials = (double)R * total_sweeps * n;
#ifdef __linux__
    if (pmu)
        pmu_start();
#else
    if (pmu)
        fprintf(stderr, "--pmu は Linux だけ\n");
#endif
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (nthreads == 1) {
        worker(NULL);
    } else {
        pthread_t *th_ = malloc(sizeof(pthread_t) * nthreads);
        for (int i = 0; i < nthreads; i++)
            pthread_create(&th_[i], NULL, worker, NULL);
        for (int i = 0; i < nthreads; i++)
            pthread_join(th_[i], NULL);
        free(th_);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
#ifdef __linux__
    if (pmu)
        pmu_stop(trials);
#endif
    double sec = (t1.tv_sec - t0.tv_sec) + 1e-9 * (t1.tv_nsec - t0.tv_nsec);

    printf("# n=%d d=%d edges=%d R=%d seed=%" PRIu64 " stages=%d sweeps=%" PRIu64
           " order=seq rng=xs64\n",
           n, d, n * d / 2, R, master_seed, S, total_sweeps);
    if (!quiet) {
        printf("# p seed c0 best best_t final");
        for (int j = 0; j < ntargets; j++)
            printf(" hit_t[%d]", targets[j]);
        printf("\n");
        for (int p = 0; p < R; p++) {
            result_t *rp = &res[p];
            printf("%d %016" PRIx64 " %d %d", p, rp->seed, rp->c0, rp->best);
            print_t(rp->best_t);
            printf(" %d", rp->final);
            for (int j = 0; j < ntargets; j++)
                print_t(rp->hit_t[j]);
            printf("\n");
        }
    }
    long long sum = 0;
    int32_t mx = res[0].best, mn = res[0].best;
    for (int p = 0; p < R; p++) {
        sum += res[p].best;
        if (res[p].best > mx)
            mx = res[p].best;
        if (res[p].best < mn)
            mn = res[p].best;
    }
    printf("summary best: mean=%.3f min=%d max=%d\n", (double)sum / R, mn, mx);
    for (int j = 0; j < ntargets; j++) {
        int hits = 0;
        for (int p = 0; p < R; p++)
            hits += res[p].hit_t[j] != NONE;
        printf("summary target %d: %d/%d\n", targets[j], hits, R);
    }

    fprintf(stderr, "time %.3f s, %.3g trials/s (threads=%d, engine=%s)\n", sec, trials / sec,
            nthreads, eng->name);

    if (init_path)
        dump_colors(init_path, colors0);
    if (final_path)
        dump_colors(final_path, colors);
    return 0;
}
