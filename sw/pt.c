/*
 * pt.c — レプリカ交換法（パラレルテンパリング）の CPU 版。回路にするときの正解になる形で書く。
 *
 * 回路に合わせた決めごと:
 *   - M 個（= 回路の 1 レーンの P = 16）のレプリカを 1 つの「系」とし、系の中だけで交換する
 *   - 温度は固定のはしご T_0 < … < T_{M−1}（.ladder、make_ladder.py が整数にしたもの）。焼きなましの段は無い
 *   - 交換するのはレプリカの塗り分けではなく温度の番号（tidx）。系の中の温度 k にいるレプリカを perm[k] で持つ
 *   - 1 試行の規則は sa.c と同じで、しきい値だけがレプリカの温度の行 TH[tidx] になる
 *   - E 周回ごと（周回 s の終わりで (s+1) % E == 0）に交換を決める。q 回目の交換では q の偶奇で
 *     組 (0,1),(2,3),… と (1,2),(3,4),… を交互に使う。組 (k, k+1) ごとに系の交換用の xorshift64 を 1 回進め、
 *       D = C[perm[k]] − C[perm[k+1]]（冷たい側 − 熱い側）、交換 = (D ≤ 0) または (r < SW[k][min(D, DMAX)])
 *   - --lag 1: 周回 s の終わりに決めた交換を、周回 s+1 の終わりに当てる（回路が 1 周回かけて決められるように）。
 *     各交換の時点では「前の決定を当ててから、いまの C で次を決める」
 *   - 種: レプリカ p は sa.c と同じ（主種から始まる splitmix64 の列の p 番目の区間）。系 L の交換用の乱数は
 *     区間 2^31 + L の最初の値（0 なら次）
 *
 * 標準出力: レプリカごとに「p seed c0 best best_t final hit_t… tidx」、続けて系ごとのまとめ（決定的）。時間は標準エラーへ。
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TARGETS 8
#define NONE UINT64_MAX
#define GOLDEN 0x9E3779B97F4A7C15ull
#define MMAX 64

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

static int n, d, M, dmax;
static uint32_t *adj, *TH, *SW; /* TH: M×(d+1)、SW: (M−1)×dmax */
static uint64_t sweeps_total, exch_every = 1;
static int lag;
static int ntargets, quiet, nthreads = 1, R; /* R = 系の数 */
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

typedef struct {
    uint64_t x;
    int32_t c, best;
    uint64_t best_t, hit_t[MAX_TARGETS];
    int next_target;
} rep_t;

/* レプリカ 1 つを 1 周回（周回 s）進める */
static void sweep(rep_t *r, uint8_t *s, uint64_t sw, const uint32_t *T)
{
    uint64_t t = sw * (uint64_t)n;
    for (int v = 0; v < n; v++, t++) {
        uint32_t rnd = (uint32_t)(xs64(&r->x) >> 32);
        const uint32_t *nb = adj + v * d;
        uint8_t sv = s[v];
        int ns = 0;
        for (int j = 0; j < d; j++)
            ns += s[nb[j]] == sv;
        int dc = 2 * ns - d;
        if (dc >= 0 || rnd < T[ns]) {
            s[v] = sv ^ 1;
            r->c += dc;
            if (r->c > r->best) {
                r->best = r->c;
                r->best_t = t;
                while (r->next_target < ntargets && r->c >= targets[r->next_target])
                    r->hit_t[r->next_target++] = t;
            }
        }
    }
}

static uint64_t decide(int q, const int *perm, const rep_t *rp, uint64_t *ex)
{
    uint64_t mask = 0;
    for (int k = q & 1; k + 1 < M; k += 2) {
        uint32_t r = (uint32_t)(xs64(ex) >> 32);
        int32_t D = rp[perm[k]].c - rp[perm[k + 1]].c;
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

static void run_system(int L)
{
    rep_t rp[MMAX];
    int perm[MMAX], tidx[MMAX];
    for (int j = 0; j < M; j++) {
        int p = L * M + j;
        uint8_t *s = colors + (size_t)p * n;
        res[p].seed = init_replica(p, s);
        rp[j].x = res[p].seed;
        rp[j].c = rp[j].best = res[p].c0 = cut_value(s);
        rp[j].best_t = NONE;
        rp[j].next_target = 0;
        for (int i = 0; i < MAX_TARGETS; i++)
            rp[j].hit_t[i] = NONE;
        perm[j] = tidx[j] = j;
    }
    uint64_t st = master_seed + ((uint64_t)(0x80000000u + (uint32_t)L) << 32) * GOLDEN;
    uint64_t ex;
    do
        ex = sm64(&st);
    while (ex == 0);
    uint64_t pending = 0;
    int q = 0;
    for (uint64_t sw = 0; sw < sweeps_total; sw++) {
        for (int j = 0; j < M; j++)
            sweep(&rp[j], colors + (size_t)(L * M + j) * n, sw, TH + tidx[j] * (d + 1));
        if ((sw + 1) % exch_every == 0) {
            if (lag) {
                apply(pending, perm, tidx);
                pending = decide(q++, perm, rp, &ex);
            } else {
                apply(decide(q++, perm, rp, &ex), perm, tidx);
            }
        }
    }
    for (int j = 0; j < M; j++) {
        result_t *r = &res[L * M + j];
        r->best = rp[j].best;
        r->best_t = rp[j].best_t;
        r->final = rp[j].c;
        memcpy(r->hit_t, rp[j].hit_t, sizeof r->hit_t);
        r->tidx = tidx[j];
        if (cut_value(colors + (size_t)(L * M + j) * n) != rp[j].c) {
            fprintf(stderr, "系 %d のレプリカ %d: カット値が合わない\n", L, j);
            exit(1);
        }
    }
}

static void *worker(void *arg)
{
    for (int L = (int)(intptr_t)arg; L < R; L += nthreads)
        run_system(L);
    return NULL;
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
    fprintf(stderr, "使い方: pt -g graph.adj -l ladder.ladder -w 周回数 [-R 系の数] [-S 主種] [-t 目標]...\n"
                    "         [-e 交換の間隔（周回）] [--lag 0|1] [-j スレッド数] [-q]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *gpath = NULL, *lpath = NULL;
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
        else
            usage();
    }
    if (!gpath || !lpath || !sweeps_total || R < 1 || exch_every < 1)
        usage();
    load_graph(gpath);
    load_ladder(lpath);
    res = calloc((size_t)R * M, sizeof *res);
    colors = malloc((size_t)R * M * n);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_t *th = malloc(sizeof(pthread_t) * nthreads);
    for (int i = 0; i < nthreads; i++)
        pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
    for (int i = 0; i < nthreads; i++)
        pthread_join(th[i], NULL);
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
    /* 系ごと: 最良と、目標に届いた最初の試行番号（系の中の最小） */
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
    return 0;
}
